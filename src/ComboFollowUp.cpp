#include "ComboFollowUp.h"
#include "AttackCoordinator.h"
#include "Boss.h"
#include "ClipTiming.h"
#include "Config.h"
#include "Intents.h"
#include "Logger.h"
#include "SCARIntegration.h"
#include "ThreadSafeMap.h"

#include <random>


namespace CombatAI::ComboFollowUp
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        // Per actor: when each window opened (epoch = closed) and whether this window already had its roll
        struct Window
        {
            Clock::time_point normal{}, power{};
            bool normalRolled = false, powerRolled = false;
        };
        ThreadSafeMap<RE::FormID, Window> g_windows;
        constexpr auto kStale = std::chrono::milliseconds(1500); // a window whose close event we missed
        bool g_installed = false;

        enum class Tag
        {
            None,
            OpenNormal,
            OpenPower,
            CloseNormal,
            ClosePower,
            CloseAll
        };

        Tag Classify(const char *a_tag)
        {
            if (!a_tag || !*a_tag) {
                return Tag::None;
            }
            const auto is = [&](const char *a_name) { return _stricmp(a_tag, a_name) == 0; };
            if (is("MCO_WinOpen") || is("BFCO_NextWinStart")) {
                return Tag::OpenNormal;
            }
            if (is("MCO_PowerWinOpen") || is("BFCO_NextPowerWinStart")) {
                return Tag::OpenPower;
            }
            if (is("MCO_WinClose")) {
                return Tag::CloseNormal;
            }
            if (is("MCO_PowerWinClose")) {
                return Tag::ClosePower;
            }
            if (is("BFCO_DIY_EndLoop") || is("MCO_Recovery") || is("BFCO_DIY_recovery") || is("attackStop")) {
                return Tag::CloseAll;
            }
            return Tag::None;
        }

        void Note(RE::FormID a_formID, Tag a_tag, const char *a_name)
        {
            const auto now = Clock::now();
            g_windows.ModifyOrCreate(a_formID, [&](Window &w) {
                switch (a_tag) {
                case Tag::OpenNormal:
                    w.normal = now;
                    w.normalRolled = false;
                    break;
                case Tag::OpenPower:
                    w.power = now;
                    w.powerRolled = false;
                    break;
                case Tag::CloseNormal:
                    w.normal = {};
                    break;
                case Tag::ClosePower:
                    w.power = {};
                    break;
                default:
                    w.normal = {};
                    w.power = {};
                    break;
                }
            });
            if (Config::GetInstance().IsDebugTarget(a_formID) && a_tag != Tag::CloseAll) {
                LOG_DEBUG("Combo 0x{:08X}: {}", a_formID, a_name);
            }
        }

        struct AnimEvent
        {
            static RE::BSEventNotifyControl thunk(RE::BSTEventSink<RE::BSAnimationGraphEvent> *a_sink,
                                                  RE::BSAnimationGraphEvent *a_event,
                                                  RE::BSTEventSource<RE::BSAnimationGraphEvent> *a_source)
            {
                if (a_event && a_event->holder) {
                    const char *tag = a_event->tag.c_str();
                    if (const auto kind = Classify(tag); kind != Tag::None) {
                        Note(a_event->holder->GetFormID(), kind, tag);
                    }
                }
                return func(a_sink, a_event, a_source);
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        bool Roll(float a_chance)
        {
            static thread_local std::mt19937 gen{std::random_device{}()};
            return std::uniform_real_distribution<float>(0.0f, 1.0f)(gen) < a_chance;
        }

        // Start the next attack on the main thread the way vanilla's Attack node and SCAR's attack-data path do: a
        // CombatAnimation carrying the attack event. Run 51 (BFCO): every chained combo followed an "attackStart"
        // inside the window; the bare attack action (it played BFCOAttackstart_0) never chained on its own.
        // The target's speed toward the NPC (+) or away (-), units/s
        float Closing(RE::Actor *a_target, const ActorStateData &a_state)
        {
            RE::NiPoint3 velocity;
            a_target->GetLinearVelocity(velocity);
            RE::NiPoint3 toSelf = a_state.self.position - a_state.target.position;
            toSelf.z = 0.0f;
            return toSelf.Unitize() > 1.0f ? velocity.x * toSelf.x + velocity.y * toSelf.y : 0.0f;
        }

        // Whether a seen attack's first blow reaches the target: the distance less the target's approach over its
        // startup against weapon reach + body radius + its forward motion
        bool Reaches(const ClipTiming::KnownSlot &a_attack, const ActorStateData &a_state, float a_closing)
        {
            const float reach = (a_state.weaponReach > 0.0f ? a_state.weaponReach : 150.0f) + a_state.target.boundRadius;
            return a_state.target.distance - a_closing * a_attack.startup <= reach + a_attack.forward;
        }

        // The slot a follow-up should play (0: leave the moveset's choice). MCO and BFCO clips write the next slot to
        // MCO_nextattack / MCO_nextpowerattack (PIE.@SGVI); a write before attackStart plays that slot (1.8.4 probe:
        // 8 of 8 forced light slots played). The moveset's own next attack stays when it reaches the target at its
        // blow; else the fastest seen attack whose lunge does (distance less the target's approach over the startup
        // against reach + body radius + forward motion). While the player recovers from a swing (its clip's blows
        // past), the fastest attack that reaches, as the punish does. Unknown slots (not played yet) are left alone.
        int ChooseSlot(RE::Actor *a_actor, RE::Actor *a_target, const ActorStateData &a_state, bool a_power,
                       std::string &a_why)
        {
            std::int32_t graph = 0;
            if (!a_actor->GetGraphVariableInt(a_power ? "MCO_nextpowerattack" : "MCO_nextattack", graph) || graph <= 0) {
                a_why = "no next-slot variable";
                return 0;
            }
            const auto known = ClipTiming::KnownSlots(a_actor->GetFormID(), a_power);
            const ClipTiming::KnownSlot *next = nullptr;
            for (const auto &k : known) {
                if (k.slot == graph) {
                    next = &k;
                }
            }
            const float closing = Closing(a_target, a_state);
            const auto reaches = [&](const ClipTiming::KnownSlot &a_k) { return Reaches(a_k, a_state, closing); };
            const ClipTiming::KnownSlot *fastest = nullptr;
            for (const auto &k : known) {
                if (reaches(k) && (!fastest || k.startup < fastest->startup)) {
                    fastest = &k;
                }
            }
            const bool punishing = a_state.target.isPlayer && a_state.target.isAttacking &&
                                   std::strcmp(a_state.temporal.target.timingSource, "none") == 0;
            if (punishing && fastest && fastest->slot != graph && (!next || fastest->startup < next->startup)) {
                a_why = std::format("you are recovering: its fastest that reaches ({:.2f}s{})", fastest->startup,
                                    next ? std::format(" vs {:.2f}s", next->startup) : std::string());
                return fastest->slot;
            }
            if (!next) {
                a_why = std::format("next slot {} not seen yet", graph);
                return 0;
            }
            if (reaches(*next)) {
                a_why = std::format("next slot {} reaches", graph);
                return 0;
            }
            if (fastest) {
                a_why = std::format("next slot {} falls short (forward {:.0f}); slot {} reaches (forward {:.0f}, you {:+.0f}/s)",
                                    graph, next->forward, fastest->slot, fastest->forward, closing);
                return fastest->slot;
            }
            a_why = std::format("no seen attack reaches at {:.0f}", a_state.target.distance);
            return 0;
        }

        void Perform(RE::Actor *a_actor, RE::Actor *a_target, bool a_power, int a_slot)
        {
            SKSE::GetTaskInterface()->AddTask([self = a_actor->GetHandle(), target = a_target->GetHandle(), a_power, a_slot]() {
                auto me = self.get();
                auto them = target.get();
                if (!me || !them) {
                    return;
                }
                auto *anim = RE::CombatAnimation::Create(me.get(), a_power ? RE::CombatAnimation::ANIM::kActionRightPowerAttack
                                                                           : RE::CombatAnimation::ANIM::kActionRightAttack);
                if (!anim) {
                    return;
                }
                anim->target = RE::NiPointer<RE::TESObjectREFR>(them.get());
                anim->animEvent = a_power ? "attackPowerStartInPlace" : "attackStart";
                if (a_slot > 0) {
                    me->SetGraphVariableInt(a_power ? "MCO_nextpowerattack" : "MCO_nextattack", a_slot);
                    if (Config::GetInstance().IsDebugTarget(me->GetFormID())) {
                        ClipTiming::ExpectOwnClip(me->GetFormID(), a_power, a_slot); // logs whether it played
                    }
                }
                // Combo branch probe (moveset proposal step 5): the MCO/BFCO clips set the next slot with
                // PIE.@SGVI|MCO_nextattack|N (power: MCO_nextpowerattack) before the window opens; write another slot
                // this NPC has played before and see whether the graph plays it
                if (Config::GetInstance().GetModIntegrations().comboBranchProbe &&
                    Config::GetInstance().IsDebugTarget(me->GetFormID())) {
                    const char *variable = a_power ? "MCO_nextpowerattack" : "MCO_nextattack";
                    std::int32_t was = -1, now = -1;
                    const bool exists = me->GetGraphVariableInt(variable, was);
                    const int slot = exists ? ClipTiming::OtherKnownSlot(me->GetFormID(), a_power, was) : 0;
                    bool set = false;
                    if (slot > 0) {
                        set = me->SetGraphVariableInt(variable, slot);
                        me->GetGraphVariableInt(variable, now);
                        ClipTiming::ExpectOwnClip(me->GetFormID(), a_power, slot);
                    }
                    LOG_DEBUG("Combo branch 0x{:08X}: {} {} -> {}", me->GetFormID(), variable,
                              exists ? std::format("was {}", was) : std::string("not in the graph"),
                              slot > 0 ? std::format("wrote {} ({}), reads {}", slot, set ? "set" : "refused", now)
                                       : std::string("no other known slot, left as is"));
                }
                const bool ok = anim->Execute();
                anim->~CombatAnimation();
                RE::free(anim);
                if (Config::GetInstance().IsDebugTarget(me->GetFormID())) {
                    LOG_DEBUG("Combo 0x{:08X}: {} follow-up {}", me->GetFormID(), a_power ? "power" : "normal",
                              ok ? "started" : "FAILED (graph refused)");
                }
            });
        }
    } // namespace

    void Install()
    {
        if (SCARIntegration::GetInstance().IsAvailable()) {
            LOG_INFO("Combo follow-up: off (SCAR.dll is loaded and runs combos)");
            return;
        }
        REL::Relocation<std::uintptr_t> sink{RE::Character::VTABLE[2]};
        AnimEvent::func = sink.write_vfunc(0x1, AnimEvent::thunk);
        g_installed = true;
        LOG_INFO("Combo follow-up: MCO/BFCO window events hooked (next attack = attackStart / attackPowerStartInPlace)");
    }

    void Update(RE::Actor *a_actor, const ActorStateData &a_state, const DecisionResult &a_decision)
    {
        const auto &settings = Config::GetInstance().GetModIntegrations();
        if (!g_installed || !settings.enableComboFollowUp || !a_actor || !a_state.target.isValid ||
            a_state.self.attackState == RE::ATTACK_STATE_ENUM::kNone) {
            return;
        }
        const RE::FormID formID = a_actor->GetFormID();
        const auto window = g_windows.Find(formID);
        if (!window) {
            return;
        }
        const auto now = Clock::now();
        const auto open = [&](Clock::time_point a_since, bool a_rolled) {
            return !a_rolled && a_since != Clock::time_point{} && now - a_since < kStale;
        };
        const bool normalOpen = open(window->normal, window->normalRolled);
        const bool powerOpen = open(window->power, window->powerRolled);
        if (!normalOpen && !powerOpen) {
            return;
        }

        // Not now (the window stays unrolled, a later tick inside it may still go)
        const bool debug = Config::GetInstance().IsDebugTarget(formID);
        const char *skip = nullptr;
        switch (a_decision.action) {
        case ActionType::Dodge:
        case ActionType::Parry:
        case ActionType::TimedBlock:
        case ActionType::Retreat:
        case ActionType::Backoff:
        case ActionType::Jump:
            skip = "defending";
            break;
        default:
            break;
        }
        if (!skip && AttackCoordinator::GetSingleton().IsHeld(formID)) {
            skip = "waiting its turn";
        }
        if (!skip && Intents::WantsToDrink(a_actor)) {
            skip = "backing off to drink";
        }
        // Reach. A combo step lunges forward: with branch choice on and attacks of that kind (light, power) seen, one
        // of them must reach the target at its blow (lunge and the target's movement: 60-170 units of forward motion
        // in the 1.7.9-1.7.10 runs); a kind not seen yet keeps up to 1.3x the attack range (run 52: 81 skips, the
        // Berserker's median at 160 against a range of ~144)
        const auto &matrix = Config::GetInstance().GetDecisionMatrix();
        const float range = StateHelpers::EffectiveAttackRange(a_state.weaponReach, a_state.target.boundRadius,
                                                               matrix.offenseReachMultiplier, matrix.offenseReachSafetyMargin);
        const bool withinRange = a_state.target.distance <= range * settings.comboReachMult;
        auto *target = skip ? nullptr : RE::TESForm::LookupByID<RE::Actor>(a_state.target.targetFormID);
        if (!skip && !target) {
            skip = "no target";
        }
        bool lightReaches = withinRange, powerReaches = withinRange;
        std::string reachLog; // debug: distance, the target's speed, each kind's verdict
        if (!skip && settings.comboBranchChoice) {
            const float closing = Closing(target, a_state);
            const float reach = (a_state.weaponReach > 0.0f ? a_state.weaponReach : 150.0f) + a_state.target.boundRadius;
            // Per kind: the seen attack that comes closest (its reach at the blow less the distance then; >= 0 reaches)
            const auto judge = [&](bool a_power, std::string &a_log) {
                const auto known = ClipTiming::KnownSlots(formID, a_power);
                if (known.empty()) {
                    a_log = std::format("{} none seen: 1.3x range {:.0f} {}", a_power ? "power" : "light",
                                        range * settings.comboReachMult, withinRange ? "reaches" : "short");
                    return withinRange;
                }
                const ClipTiming::KnownSlot *best = nullptr;
                float bestMargin = 0.0f;
                for (const auto &k : known) {
                    const float margin = reach + k.forward - (a_state.target.distance - closing * k.startup);
                    if (!best || margin > bestMargin) {
                        best = &k;
                        bestMargin = margin;
                    }
                }
                a_log = std::format("{} best slot {} ({:.2f}s, forward {:.0f}): {} {:.0f}", a_power ? "power" : "light",
                                    best->slot, best->startup, best->forward, bestMargin >= 0.0f ? "reaches by" : "short by",
                                    std::abs(bestMargin));
                return bestMargin >= 0.0f; // same test as Reaches()
            };
            std::string lightLog, powerLog;
            lightReaches = judge(false, lightLog);
            powerReaches = judge(true, powerLog);
            if (debug) {
                reachLog = std::format("; dist {:.0f}, you {:+.0f}/s toward it, reach {:.0f}; {}; {}", a_state.target.distance,
                                       closing, reach, lightLog, powerLog);
            }
        }
        const bool normalGo = normalOpen && lightReaches, powerGo = powerOpen && powerReaches;
        if (!skip && !normalGo && !powerGo) {
            skip = "target out of reach"; // the window stays unrolled: the target may still come within reach
        }
        if (skip) {
            if (debug) {
                LOG_DEBUG("Combo 0x{:08X}: window open, no follow-up ({}){}", formID, skip,
                          std::strcmp(skip, "target out of reach") == 0 ? reachLog : std::string());
            }
            return;
        }

        // One roll per window: power when its window is open and stamina allows, else normal
        const float powerMin = Boss::PowerStaminaMin(a_actor);
        bool power = false, go = false;
        // An enraged boss (or a brave desperate one) chains more and harder: x1.5 power, x1.3 normal
        const auto phase = Boss::CurrentPhase(a_actor);
        const bool raging = phase == Boss::Phase::Enraged || (phase == Boss::Phase::Desperate && Boss::Brave(a_actor));
        // No light attack reaches but a power one does: the power attack stands in for the light follow-up, at its
        // chance (1.9.2 run: 4 windows, power reaching by 18-58, lost the 30% power roll and passed with nothing)
        const bool powerOnly = normalOpen && !lightReaches && powerGo;
        if (powerGo && a_state.self.staminaPercent >= powerMin) {
            const float chance = powerOnly ? (std::max)(settings.comboPowerChance, settings.comboFollowUpChance)
                                           : settings.comboPowerChance;
            power = go = Roll((std::min)(1.0f, chance * (raging ? 1.5f : 1.0f)));
        }
        if (!go && normalGo) {
            go = Roll((std::min)(1.0f, settings.comboFollowUpChance * (raging ? 1.3f : 1.0f)));
        }
        g_windows.Modify(formID, [&](Window &w) {
            w.powerRolled |= powerGo;
            w.normalRolled |= normalGo || go;
            if (go) {
                w.powerRolled = true;
            }
        });
        if (debug) {
            LOG_DEBUG("Combo 0x{:08X}: window open (normal {}, power {}; in reach {}/{}{}) -> {}", formID, normalOpen,
                      powerOpen, lightReaches, powerReaches, reachLog,
                      go ? (power ? (powerOnly ? "power follow-up (only power reaches)" : "power follow-up")
                                  : "normal follow-up")
                         : "no follow-up (roll)");
        }
        if (go) {
            // Which attack (the probe, a test, forces its own slot in Perform instead)
            int slot = 0;
            if (settings.comboBranchChoice && !(settings.comboBranchProbe && debug)) {
                std::string why;
                slot = ChooseSlot(a_actor, target, a_state, power, why);
                if (debug) {
                    LOG_DEBUG("Combo 0x{:08X}: {} branch: {} -> {}", formID, power ? "power" : "light", why,
                              slot > 0 ? std::format("slot {}", slot) : std::string("the moveset's choice"));
                }
            }
            Perform(a_actor, target, power, slot);
        }
    }
} // namespace CombatAI::ComboFollowUp
