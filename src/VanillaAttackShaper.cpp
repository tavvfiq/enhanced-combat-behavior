#include "VanillaAttackShaper.h"
#include "Boss.h"
#include "ActorStateData.h"
#include "ActorUtils.h"
#include "AttackCoordinator.h"
#include "Config.h"
#include "Intents.h"
#include "LimbReach.h"
#include "Logger.h"
#include "pch.h"

namespace CombatAI
{
    namespace
    {
        float Logged(RE::Actor *a_attacker, RE::BGSAttackData *a_data, float a_chance, float a_result,
                     const char *a_reason)
        {
            const RE::FormID formID = a_attacker->GetFormID();
            if (Config::GetInstance().GetGeneral().verboseTicks && Config::GetInstance().IsDebugTarget(formID)) { // per tick: bulky
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
        // Brain creatures mid-swing: their graph refuses a new start for ~2 s (attack state reads idle meanwhile);
        // a beast with limb reach only until its first blow, so its combos go on
        if (a_attacker && a_chance > 0.0f && LimbReach::CreatureStillSwinging(a_attacker)) {
            return Logged(a_attacker, a_data, a_chance, 0.0f, "creature mid-swing");
        }
        // Beasts with measured limbs (LimbReach): an attack starts only when its first blow reaches the target, and a
        // short one only when no longer attack of its kind (30+ more) reaches too. Long attacks hit close targets fine
        // (werewolf, 1.22.0 run 13: hands 117-134 landed 13 of 18, some 18-43 inside the hand), while the short side
        // and running swings (68-99) landed 0 of 13 when kept for close range: a step back put them out of reach. Attacks not yet measured
        // pass, so each gets measured. Vanilla trolls started attacks from up to 320 units and landed 5 of 25.
        bool beastFits = false; // a measured attack that reaches: may stand in for the brain's other kind
        if (a_attacker && a_target && a_data && a_chance > 0.0f && LimbReach::Active(a_attacker) &&
            !a_data->data.flags.any(RE::AttackData::AttackFlag::kBashAttack)) {
            if (const float reach = LimbReach::Reach(a_attacker, a_data->event.c_str()); reach > 0.0f) {
                // Where the target will be at the blow: a werewolf's blows came ~0.4 s in, and 16 of its 35 misses had
                // the player 36-196 past the hand by then, backing off (1.22.0 run 14)
                const float distance =
                    LimbReach::DistanceAtBlow(a_attacker, a_target, LimbReach::Startup(a_attacker, a_data->event.c_str()));
                const float radius = ActorUtils::GetBodyRadius(a_target);
                const bool power = a_data->data.flags.any(RE::AttackData::AttackFlag::kPowerAttack);
                if (distance > LimbReach::ContactDistance(reach, radius)) {
                    return Logged(a_attacker, a_data, a_chance, 0.0f, "veto: beast attack falls short");
                }
                if (LimbReach::AnyReaches(a_attacker, a_target, power, reach + 30.0f)) {
                    return Logged(a_attacker, a_data, a_chance, 0.0f, "veto: beast short attack, a longer one reaches");
                }
                beastFits = !power || !LacksPowerStamina(a_attacker);
            }
        }

        if (a_attacker && a_data && a_chance > 0.0f) {
            const auto committed = Intents::CommittedAttack(a_attacker);
            if (committed != Intents::Attack::None) {
                const auto flags = a_data->data.flags;
                const bool power = flags.any(RE::AttackData::AttackFlag::kPowerAttack);
                // A beast whose committed kind has no measured attack that reaches takes a reaching one of the other
                // kind, or the brain's attack stays refused at that distance
                const bool committedPower = committed == Intents::Attack::Power;
                const bool standIn = beastFits && power != committedPower && !LimbReach::AnyReaches(a_attacker, a_target, committedPower);
                const bool wanted = !flags.any(RE::AttackData::AttackFlag::kBashAttack) &&
                                    (power == committedPower || standIn);
                if (wanted) {
                    Intents::NoteAttackMatched(a_attacker);
                }
                return Logged(a_attacker, a_data, a_chance, wanted ? 1.0f : 0.0f,
                              wanted ? "intent" : "intent: other kind");
            }
        }

        // A beast's own vanilla start (no brain intent) from beyond the brain's attack range: vanilla trolls started
        // 2 of 3 attacks themselves, from up to 320 units, and landed 5 of 25 (1.22.0 test run; the brain's own: 5 of 12).
        // A measured attack that reaches passed the check above and keeps its own reach.
        if (a_attacker && a_target && a_chance > 0.0f && !beastFits && ActorUtils::IsBeastCreature(a_attacker)) {
            const float range = Intents::AttackRange(a_attacker);
            if (range > 0.0f && a_attacker->GetPosition().GetDistance(a_target->GetPosition()) > range) {
                return Logged(a_attacker, a_data, a_chance, 0.0f, "veto: beast beyond the brain's attack range");
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
