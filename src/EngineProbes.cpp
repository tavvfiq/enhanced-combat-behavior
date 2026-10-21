#include "EngineProbes.h"
#include "Config.h"
#include "Logger.h"
#include "pch.h"

namespace CombatAI::EngineProbes
{
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
