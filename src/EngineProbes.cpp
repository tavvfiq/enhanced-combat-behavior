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
    } // namespace

    void Install()
    {
        REL::Relocation<std::uintptr_t> holderVTable{RE::Character::VTABLE[3]};
        NotifyGraph::func = holderVTable.write_vfunc(0x1, NotifyGraph::thunk);
        LOG_INFO("Probe: NPC attack/bash/block animation events hooked (debug log, selected NPC)");
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
