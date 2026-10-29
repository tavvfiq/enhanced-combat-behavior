#include "VanillaAttackShaper.h"
#include "Boss.h"
#include "ActorStateData.h"
#include "ActorUtils.h"
#include "AttackCoordinator.h"
#include "Config.h"
#include "Intents.h"
#include "Logger.h"
#include "SCARIntegration.h"
#include "pch.h"

namespace CombatAI
{
    namespace
    {
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

        // True when a_actor is below the stamina fraction power moves need
        bool LacksPowerStamina(RE::Actor *a_actor)
        {
            const float minimum = Boss::PowerStaminaMin(a_actor);
            auto *values = ActorUtils::SafeAsActorValueOwner(a_actor);
            const float max = a_actor->GetActorValueMax(RE::ActorValue::kStamina);
            return minimum > 0.0f && values && max > 0.0f && values->GetActorValue(RE::ActorValue::kStamina) < minimum * max;
        }
    } // namespace

    float VanillaAttackShaper::Adjust(RE::Actor *a_attacker, RE::Actor *a_target, RE::BGSAttackData *a_data,
                                      float a_chance)
    {
        // An attack the brain asked for (taken by our Melee tree) picks only attack data of its
        // kind, and 1.0 passes StartAttack's second roll. No match leaves no candidate, which
        // StartAttack handles as a clean failure (SE 48139+0x328).
        // Backing off to drink or coat (Potions::Update): no new swing, or it never leaves the swing (run 1.3.5)
        if (a_attacker && a_data && a_chance > 0.0f && Intents::WantsToDrink(a_attacker)) {
            return Logged(a_attacker, a_data, a_chance, 0.0f, "veto: backing off to drink");
        }
        // Humanoid creatures mid-swing: their graph refuses a new start for ~2 s (attack state reads idle meanwhile)
        if (a_attacker && a_chance > 0.0f && ActorUtils::IsHumanoidCreature(a_attacker) &&
            Intents::SinceSwing(a_attacker) < Config::GetInstance().GetDecisionMatrix().creatureSwingSeconds) {
            return Logged(a_attacker, a_data, a_chance, 0.0f, "creature mid-swing");
        }
        if (a_attacker && a_data && a_chance > 0.0f) {
            const auto committed = Intents::CommittedAttack(a_attacker);
            if (committed != Intents::Attack::None) {
                const auto flags = a_data->data.flags;
                const bool power = flags.any(RE::AttackData::AttackFlag::kPowerAttack);
                const bool wanted = !flags.any(RE::AttackData::AttackFlag::kBashAttack) &&
                                    power == (committed == Intents::Attack::Power);
                if (wanted) {
                    Intents::NoteAttackMatched(a_attacker);
                }
                return Logged(a_attacker, a_data, a_chance, wanted ? 1.0f : 0.0f,
                              wanted ? "intent" : "intent: other kind");
            }
        }

        const auto &settings = Config::GetInstance().GetVanillaAttacks();
        if (!settings.enable || a_chance <= 0.0f || !a_attacker || !a_target || !a_data) {
            return a_chance;
        }

        const RE::FormID formID = a_attacker->GetFormID();
        // Pacing: waiting its turn
        if (AttackCoordinator::GetSingleton().IsHeld(formID)) {
            return Logged(a_attacker, a_data, a_chance, 0.0f, "veto: waiting its turn");
        }

        // SCAR-patched movesets pick their own attacks
        if (SCARIntegration::GetInstance().IsDriving(formID)) {
            return Logged(a_attacker, a_data, a_chance, a_chance, "SCAR moveset");
        }

        const bool power = a_data->data.flags.any(RE::AttackData::AttackFlag::kPowerAttack);
        // Power attacks and power bashes need stamina; light attacks and plain bashes never do
        if (power && LacksPowerStamina(a_attacker)) {
            return Logged(a_attacker, a_data, a_chance, 0.0f, "power: stamina too low");
        }
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
} // namespace CombatAI
