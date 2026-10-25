#include "EngineProbes.h"
#include "Config.h"
#include "Logger.h"
#include "pch.h"

namespace CombatAI::EngineProbes
{
    namespace
    {
        // Attack, bash and block events only (dodge logs its own result in DodgeSystem)
        bool IsCombatEvent(std::string_view a_name)
        {
            std::string lower(a_name);
            std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return lower.contains("attack") || lower.contains("bash") || lower.contains("block");
        }

        struct NotifyGraph
        {
            static bool thunk(RE::IAnimationGraphManagerHolder *a_this, const RE::BSFixedString &a_event)
            {
                const bool accepted = func(a_this, a_event);
                // This vtable belongs to Character, so a_this is always a Character's holder subobject
                const auto *actor = static_cast<RE::Character *>(a_this);
                if (Config::GetInstance().IsDebugTarget(actor->GetFormID()) && IsCombatEvent(a_event.c_str())) {
                    LOG_DEBUG("AnimEvent 0x{:08X}: {} {}", actor->GetFormID(), a_event.c_str(),
                              accepted ? "accepted" : "rejected");
                }
                return accepted;
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        // ContextMelee::StartAttack (SE 48139 / AE 49170) picks an attack, then fires it only if the hit-cone
        // check (SE 46033 / AE 47297) and CheckAttack (SE 48140 / AE 49171) both pass. Same offsets in AE
        // 1.6.1170 and 1.7.104. Args from the call setup; only the actors are read, the rest is passed through.
        thread_local RE::Actor *t_checking = nullptr; // set by the hit-cone call, read by CheckAttack right after

        struct HitCone
        {
            static bool thunk(RE::Actor *a_self, RE::Actor *a_target, void *a_a3, void *a_a4, void *a_a5, float a_a6,
                              void *a_a7, bool a_a8)
            {
                const bool pass = func(a_self, a_target, a_a3, a_a4, a_a5, a_a6, a_a7, a_a8);
                t_checking = nullptr;
                if (a_self && a_target && Config::GetInstance().IsDebugTarget(a_self->GetFormID())) {
                    t_checking = pass ? a_self : nullptr;
                    LOG_DEBUG("AttackCheck 0x{:08X}: hit cone {} (target at {:.0f} deg, dist {:.0f}, arg {:.2f})",
                              a_self->GetFormID(), pass ? "pass" : "FAIL",
                              a_self->GetHeadingAngle(a_target->GetPosition(), false), a_self->GetDistance(a_target),
                              a_a6);
                }
                return pass;
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        struct CheckAttack
        {
            static bool thunk(void *a_context, void *a_entry)
            {
                const bool pass = func(a_context, a_entry);
                if (auto *self = std::exchange(t_checking, nullptr)) {
                    LOG_DEBUG("AttackCheck 0x{:08X}: range/path check {}", self->GetFormID(), pass ? "pass" : "FAIL");
                }
                return pass;
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };
    } // namespace

    void Install()
    {
        REL::Relocation<std::uintptr_t> holderVTable{RE::Character::VTABLE[3]};
        NotifyGraph::func = holderVTable.write_vfunc(0x1, NotifyGraph::thunk);
        LOG_INFO("Probe: NPC attack/bash/block animation events hooked (debug log, selected NPC)");

        if (REL::Module::IsVR()) {
            return;
        }
        auto &trampoline = SKSE::GetTrampoline();
        REL::Relocation<std::uintptr_t> start{RELOCATION_ID(48139, 49170)};
        const auto cone = start.address() + REL::Relocate(0x493, 0x3F1);
        const auto check = start.address() + REL::Relocate(0x4A6, 0x404);
        if (*reinterpret_cast<const std::uint8_t *>(cone) != 0xE8 || *reinterpret_cast<const std::uint8_t *>(check) != 0xE8) {
            LOG_WARN("Probe: StartAttack check sites are not calls - attack check probe off");
            return;
        }
        HitCone::func = trampoline.write_call<5>(cone, HitCone::thunk);
        CheckAttack::func = trampoline.write_call<5>(check, CheckAttack::thunk);
        LOG_INFO("Probe: vanilla attack start checks hooked (hit cone, range/path; debug log, selected NPC)");
    }

    void Update(float a_deltaTime)
    {
        static float timer = 0.0f;
        if (!Config::GetInstance().GetGeneral().enableDebugLog || (timer += a_deltaTime) < 0.5f) {
            return;
        }
        timer = 0.0f;

        const auto selected = RE::Console::GetSelectedRef();
        auto *actor = selected ? selected->As<RE::Actor>() : nullptr;
        auto *manager = RE::CombatManager::GetSingleton();
        // CombatManager+0x28, verified in AE: callers load it and pass it to threat registration
        auto *threatMap = manager ? manager->combatThreatMap : nullptr;
        if (!actor || !threatMap) {
            return;
        }

        const auto pos = actor->GetPosition();
        RE::BSReadLockGuard lock(threatMap->lock);
        if (threatMap->threats.empty()) {
            return;
        }
        LOG_DEBUG("Probe Threats: {} active, selected 0x{:08X}", threatMap->threats.size(),
                  static_cast<std::uint32_t>(actor->GetFormID()));
        int shown = 0;
        for (const auto &entry : threatMap->threats) {
            const auto &threat = entry.second;
            if (!threat) {
                continue;
            }
            if (++shown > 8) {
                break;
            }
            const auto *form = RE::TESForm::LookupByID(threat->formid);
            RE::NiPointer<RE::TESObjectREFR> shooter;
            RE::TESObjectREFR::LookupByHandle(threat->shooter, shooter);
            LOG_DEBUG("  threat 0x{:08X} {} objType {} shooter 0x{:08X} dist {:.0f} rad {:.0f} dmg {:.1f} range {:.0f} "
                      "cone {:.2f} explodeAt {:.2f} speed {:.0f}",
                      static_cast<std::uint32_t>(threat->formid),
                      form ? RE::FormTypeToString(form->GetFormType()) : std::string_view("?"),
                      threat->GetObjectType(), static_cast<std::uint32_t>(shooter ? shooter->GetFormID() : 0),
                      pos.GetDistance(threat->pos), threat->rad, threat->damage, threat->range, threat->cone,
                      threat->explosion_time, threat->velocity.Length());
        }
    }
} // namespace CombatAI::EngineProbes
