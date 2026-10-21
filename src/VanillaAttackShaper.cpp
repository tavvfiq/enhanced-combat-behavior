#include "VanillaAttackShaper.h"
#include "ActorStateData.h"
#include "ActorUtils.h"
#include "AttackCoordinator.h"
#include "Config.h"
#include "Logger.h"
#include "SCARIntegration.h"
#include "pch.h"

namespace CombatAI
{
    namespace
    {
        bool IsDefensiveAction(ActionType a_action)
        {
            switch (a_action) {
            case ActionType::Dodge:
            case ActionType::Strafe:
            case ActionType::Jump:
            case ActionType::Backoff:
            case ActionType::Retreat:
            case ActionType::TimedBlock:
            case ActionType::Parry:
                return true;
            default:
                return false;
            }
        }

        float Logged(RE::Actor *a_attacker, RE::BGSAttackData *a_data, float a_chance, float a_result,
                     const char *a_reason)
        {
            const RE::FormID formID = a_attacker->GetFormID();
            if (Config::GetInstance().IsDebugTarget(formID)) {
                LOG_DEBUG("VanillaAttack 0x{:08X} {}: chance {:.2f} -> {:.2f} ({})", static_cast<std::uint32_t>(formID),
                          a_data->event.c_str(), a_chance, a_result, a_reason);
            }
            return a_result;
        }

        // Target's facing within 60 degrees of the attacker
        bool IsFacing(RE::Actor *a_target, RE::Actor *a_attacker)
        {
            const auto targetPos = ActorUtils::SafeGetPosition(a_target);
            const auto attackerPos = ActorUtils::SafeGetPosition(a_attacker);
            if (!targetPos || !attackerPos) {
                return false;
            }
            return StateHelpers::CalculateOrientationDot(*attackerPos, *targetPos,
                                                         StateHelpers::GetActorForwardVector(a_target)) > 0.5f;
        }
    } // namespace

    float VanillaAttackShaper::Adjust(RE::Actor *a_attacker, RE::Actor *a_target, RE::BGSAttackData *a_data,
                                      float a_chance)
    {
        const auto &settings = Config::GetInstance().GetVanillaAttacks();
        if (!settings.enable || a_chance <= 0.0f || !a_attacker || !a_target || !a_data) {
            return a_chance;
        }

        const RE::FormID formID = a_attacker->GetFormID();
        if (const auto lastDefense = m_lastDefense.Find(formID)) {
            if (Clock::now() - *lastDefense < std::chrono::duration<float>(settings.defenseVetoSeconds)) {
                return Logged(a_attacker, a_data, a_chance, 0.0f, "veto: our defense");
            }
        }

        // Pacing: waiting its turn
        if (AttackCoordinator::GetSingleton().IsHeld(formID)) {
            return Logged(a_attacker, a_data, a_chance, 0.0f, "veto: waiting its turn");
        }

        // SCAR-patched movesets pick their own attacks
        if (SCARIntegration::GetInstance().IsDriving(formID)) {
            return Logged(a_attacker, a_data, a_chance, a_chance, "SCAR moveset");
        }

        const bool power = a_data->data.flags.any(RE::AttackData::AttackFlag::kPowerAttack);
        const bool targetOpen = ActorUtils::SafeGetKnockState(a_target) != RE::KNOCK_STATE_ENUM::kNormal ||
                                ActorUtils::SafeGetAttackState(a_target) == RE::ATTACK_STATE_ENUM::kFollowThrough ||
                                ActorUtils::SafeWhoIsCasting(a_target) != 0;
        if (targetOpen) {
            return Logged(a_attacker, a_data, a_chance,
                          a_chance * (power ? settings.openingPowerMult : settings.openingNormalMult), "target open");
        }
        if (ActorUtils::SafeIsBlocking(a_target) && IsFacing(a_target, a_attacker)) {
            return Logged(a_attacker, a_data, a_chance,
                          a_chance * (power ? settings.blockedPowerMult : settings.blockedNormalMult), "target blocking");
        }
        return Logged(a_attacker, a_data, a_chance, a_chance, "unchanged");
    }

    void VanillaAttackShaper::OnActionExecuted(RE::FormID a_formID, ActionType a_action)
    {
        if (IsDefensiveAction(a_action)) {
            const auto now = Clock::now();
            m_lastDefense.ModifyOrCreate(a_formID, [&](Clock::time_point &a_time) { a_time = now; });
        }
    }
} // namespace CombatAI
