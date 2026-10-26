#include "ComboFollowUp.h"
#include "AttackCoordinator.h"
#include "Config.h"
#include "Logger.h"
#include "SCARIntegration.h"
#include "ThreadSafeMap.h"

#include <random>

#undef GetObject // windows.h maps it to GetObjectA, hiding BGSDefaultObjectManager::GetObject

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
        RE::TESIdleForm *g_normalIdle = nullptr;
        RE::TESIdleForm *g_powerIdle = nullptr;

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

        // Start the next attack on the main thread: the moveset's NPC attack idle when it exists, else the bare
        // attack action (BFCO: NPC combos follow the player's rules)
        void Perform(RE::Actor *a_actor, RE::Actor *a_target, bool a_power)
        {
            auto *objects = RE::BGSDefaultObjectManager::GetSingleton();
            auto *action = objects ? objects->GetObject<RE::BGSAction>(a_power ? RE::DEFAULT_OBJECT::kActionRightPowerAttack
                                                                               : RE::DEFAULT_OBJECT::kActionRightAttack)
                                   : nullptr;
            if (!action) {
                return;
            }
            RE::TESIdleForm *idle = a_power ? g_powerIdle : g_normalIdle;
            SKSE::GetTaskInterface()->AddTask([self = a_actor->GetHandle(), target = a_target->GetHandle(), action, idle, a_power]() {
                auto me = self.get();
                auto them = target.get();
                if (!me || !them) {
                    return;
                }
                const bool debug = Config::GetInstance().IsDebugTarget(me->GetFormID());
                if (idle && !idle->CheckConditions(me.get(), them.get(), false)) {
                    if (debug) {
                        LOG_DEBUG("Combo 0x{:08X}: {} follow-up refused by the idle's conditions", me->GetFormID(),
                                  a_power ? "power" : "normal");
                    }
                    return;
                }
                auto *data = RE::TESActionData::Create();
                if (!data) {
                    return;
                }
                data->source = RE::NiPointer<RE::TESObjectREFR>(me.get());
                data->target = RE::NiPointer<RE::TESObjectREFR>(them.get());
                data->action = action;
                if (idle) {
                    data->animObjIdle = idle;
                    data->animEvent = idle->animEventName;
                }
                const bool ok = data->Process();
                data->~TESActionData();
                RE::free(data);
                if (debug) {
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
        g_normalIdle = RE::TESForm::LookupByEditorID<RE::TESIdleForm>("ADXP_NPCNormalAttack");
        g_powerIdle = RE::TESForm::LookupByEditorID<RE::TESIdleForm>("ADXP_NPCPowerAttack");
        REL::Relocation<std::uintptr_t> sink{RE::Character::VTABLE[2]};
        AnimEvent::func = sink.write_vfunc(0x1, AnimEvent::thunk);
        g_installed = true;
        const auto name = [](RE::TESIdleForm *a_idle) {
            return a_idle ? std::format("found (event {})", a_idle->animEventName.c_str()) : std::string("not found");
        };
        LOG_INFO("Combo follow-up: MCO/BFCO window events hooked; MCO NPC idles ADXP_NPCNormalAttack {}, ADXP_NPCPowerAttack {}",
                 name(g_normalIdle), name(g_powerIdle));
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
        if (!skip && !a_state.combatContext.isInAttackRange) {
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
        if (powerOpen && a_state.self.staminaPercent >= powerMin) {
            power = go = Roll(settings.comboPowerChance);
        }
        if (!go && normalOpen) {
            go = Roll(settings.comboFollowUpChance);
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
