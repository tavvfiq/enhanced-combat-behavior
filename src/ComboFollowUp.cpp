#include "ComboFollowUp.h"
#include "AttackCoordinator.h"
#include "Boss.h"
#include "Config.h"
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
        void Perform(RE::Actor *a_actor, RE::Actor *a_target, bool a_power)
        {
            SKSE::GetTaskInterface()->AddTask([self = a_actor->GetHandle(), target = a_target->GetHandle(), a_power]() {
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
        // A combo step lunges forward, so up to 1.3x the attack range (run 52: 81 skips, the Berserker's median
        // at 160 against a range of ~144)
        const auto &matrix = Config::GetInstance().GetDecisionMatrix();
        const float range = StateHelpers::EffectiveAttackRange(a_state.weaponReach, a_state.target.boundRadius,
                                                               matrix.offenseReachMultiplier, matrix.offenseReachSafetyMargin);
        if (!skip && a_state.target.distance > range * 1.3f) {
            skip = "target out of reach";
        }
        auto *target = skip ? nullptr : RE::TESForm::LookupByID<RE::Actor>(a_state.target.targetFormID);
        if (!skip && !target) {
            skip = "no target";
        }
        if (skip) {
            if (debug) {
                LOG_DEBUG("Combo 0x{:08X}: window open, no follow-up ({})", formID, skip);
            }
            return;
        }

        // One roll per window: power when its window is open and stamina allows, else normal
        const float powerMin = Config::GetInstance().GetDecisionMatrix().powerAttackMinStamina;
        bool power = false, go = false;
        // An enraged boss (or a brave desperate one) chains more and harder: x1.5 power, x1.3 normal
        const auto phase = Boss::CurrentPhase(a_actor);
        const bool raging = phase == Boss::Phase::Enraged || (phase == Boss::Phase::Desperate && Boss::Brave(a_actor));
        if (powerOpen && a_state.self.staminaPercent >= powerMin) {
            power = go = Roll((std::min)(1.0f, settings.comboPowerChance * (raging ? 1.5f : 1.0f)));
        }
        if (!go && normalOpen) {
            go = Roll((std::min)(1.0f, settings.comboFollowUpChance * (raging ? 1.3f : 1.0f)));
        }
        g_windows.Modify(formID, [&](Window &w) {
            w.powerRolled |= powerOpen;
            w.normalRolled |= normalOpen || go;
            if (go) {
                w.powerRolled = true;
            }
        });
        if (debug) {
            LOG_DEBUG("Combo 0x{:08X}: window open (normal {}, power {}) -> {}", formID, normalOpen, powerOpen,
                      go ? (power ? "power follow-up" : "normal follow-up") : "no follow-up (roll)");
        }
        if (go) {
            Perform(a_actor, target, power);
        }
    }
} // namespace CombatAI::ComboFollowUp
