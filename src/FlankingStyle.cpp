// Flanking combat styles: see FlankingStyle.h. RE notes: D:\Modding\skyrim_re\COMBAT_AI_RE.md, "Flanking styles".
#include "FlankingStyle.h"
#include "CombatStyleEnhancer.h"
#include "Config.h"
#include "Intents.h"
#include "Logger.h"
#include "ThreadSafeMap.h"

namespace CombatAI::FlankingStyle
{
    namespace
    {
        constexpr std::uint32_t kFlankingFlag = 1u << 1; // TESCombatStyle::FLAG::kFlankingStyle

        // The style's DATA flags at +0x9C, read as the original does
        std::uint32_t Flags(const RE::TESCombatStyle *a_style)
        {
            return *reinterpret_cast<const std::uint32_t *>(reinterpret_cast<std::uintptr_t>(a_style) + 0x9C);
        }

        ThreadSafeMap<RE::FormID, bool> g_logged; // selected NPCs told once

        // The Movement selector's ValueNode for option 9 FlankingMovement calls HasFlag(style, kFlankingStyle) every
        // 0.25 s on the combat AI thread (SE 48462 / AE 49453: test [style+0x9C], flag; setne al; its only reference
        // is the Movement tree builder SE 48431 / AE 49436). The asking actor is the tree's current attacker.
        struct HasFlag
        {
            static bool thunk(RE::TESCombatStyle *a_style, std::uint32_t a_flag)
            {
                const bool has = a_style && (Flags(a_style) & a_flag) != 0;
                if (!has || a_flag != kFlankingFlag ||
                    !Config::GetInstance().GetDecisionMatrix().flankingStyleBrainMovement) {
                    return has;
                }
                auto *actor = RE::CombatBehaviorController::GetCurrentAttacker();
                if (!actor || !Intents::BrainDriven(actor)) {
                    return has; // not one the brain drives (animals, archers' own trees, not processed yet)
                }
                const auto id = actor->GetFormID();
                if (Config::GetInstance().IsDebugTarget(id) && !g_logged.Find(id)) {
                    g_logged.ModifyOrCreate(id, [](bool &a_b) { a_b = true; });
                    LOG_DEBUG("Flanking 0x{:08X}: flanking combat style - vanilla's flanking movement off, the brain's close "
                              "movement instead (circles to the target's back while waiting, presses from the side)",
                              static_cast<std::uint32_t>(id));
                }
                return false;
            }
        };
    } // namespace

    void Install(SKSE::Trampoline &a_trampoline)
    {
        REL::Relocation<std::uintptr_t> target{REL::RelocationID(48462, 49453)};
        // The whole function is 10 bytes: check them before the 5-byte branch goes over its start
        constexpr std::uint8_t kExpected[] = {0x85, 0x91, 0x9C, 0x00, 0x00, 0x00, 0x0F, 0x95, 0xC0, 0xC3};
        if (std::memcmp(reinterpret_cast<const void *>(target.address()), kExpected, sizeof(kExpected)) != 0) {
            LOG_WARN("Flanking: the flanking style check doesn't match - flanking NPCs keep vanilla's movement");
            return;
        }
        a_trampoline.write_branch<5>(target.address(), HasFlag::thunk);
        LOG_INFO("Flanking: style check hooked (NPCs the brain drives use its close movement)");
    }

    bool Is(RE::Actor *a_actor)
    {
        const auto *style = CombatStyleEnhancer::GetCombatStyle(a_actor);
        return style && (Flags(style) & kFlankingFlag) != 0;
    }
} // namespace CombatAI::FlankingStyle
