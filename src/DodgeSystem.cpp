#include "DodgeSystem.h"
#include "ActorUtils.h"
#include "Config.h"
#include "EnvSensor.h"
#include "pch.h"
#include <cmath>

namespace CombatAI
{
    namespace
    {
        constexpr float PI = 3.14159265f;
        constexpr float PI8 = 0.39269908f; // PI / 8
    } // namespace

    bool DodgeSystem::CanDodge(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return false;
        }

        // Check config first
        auto &config = CombatAI::Config::GetInstance();
        if (!config.GetModIntegrations().enableTKDodgeIntegration) {
            return false;
        }

        // Check if already dodging
        if (IsDodging(a_actor)) {
            return false;
        }

        // Check attack state (can cancel if enabled)
        RE::ActorState *state = ActorUtils::SafeAsActorState(a_actor);
        if (state) {
            RE::ATTACK_STATE_ENUM attackState = state->GetAttackState();
            if (attackState != RE::ATTACK_STATE_ENUM::kNone && !m_config.enableDodgeAttackCancel) {
                return false;
            }
        }

        // Check life state
        if (state) {
            if (state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal ||
                state->GetKnockState() != RE::KNOCK_STATE_ENUM::kNormal ||
                state->GetFlyState() != RE::FLY_STATE::kNone) {
                return false;
            }
        }

        // Check if swimming
        if (state && state->IsSwimming()) {
            return false;
        }

        // Check if in kill move
        if (a_actor->IsInKillMove()) {
            return false;
        }

        // Check if jumping (via animation graph)
        bool isJumping = false;
        a_actor->GetGraphVariableBool(RE::BSFixedString("bInJumpState"), isJumping);
        if (isJumping) {
            return false;
        }

        // Check stamina
        auto actorValueOwner = ActorUtils::SafeAsActorValueOwner(a_actor);
        float currentStamina = actorValueOwner->GetActorValue(RE::ActorValue::kStamina);
        if (currentStamina < (std::max)(m_config.dodgeStaminaCost, config.GetDodgeSystem().minDodgeStamina)) {
            return false;
        }

        return true;
    }

    bool DodgeSystem::ExecuteDodge(RE::Actor *a_actor, const RE::NiPoint3 &a_dodgeDirection)
    {
        if (!a_actor || !CanDodge(a_actor)) {
            return false;
        }

        // Cliff-safe dodge: never carry the actor over a ledge. Probe for ground at the
        // spot this dodge would land on; if there's a lethal drop, abort so the actor
        // picks another action instead of leaping off a cliff. Final guard for every
        // ExecuteDodge caller (evasion pre-checks and may flip sides before this).
        const auto &envCfg = ::CombatAI::Config::GetInstance().GetEnvSensor();
        if (envCfg.enableCliffSafeDodge) {
            RE::NiPoint3 flatDir = a_dodgeDirection;
            flatDir.z = 0.0f;
            flatDir.Unitize();
            if (!EnvSensor::HasGroundAhead(a_actor, flatDir, envCfg.dodgeLedgeProbeDistance, envCfg.dodgeMaxDrop)) {
                return false; // would dodge off a ledge
            }
        }

        // Determine dodge direction string
        std::string dodgeEvent = DetermineDodgeDirection(a_actor, a_dodgeDirection);

        // Set step dodge variable if enabled
        if (m_config.enableStepDodge) {
            a_actor->SetGraphVariableInt(RE::BSFixedString("iStep"), 2);
        }

        // Set i-frame duration
        a_actor->SetGraphVariableFloat(RE::BSFixedString("TKDR_IframeDuration"), m_config.iFrameDuration);

        // Trigger dodge animation; the fallback covers graphs that only take one dodge event
        bool success = a_actor->NotifyAnimationGraph(RE::BSFixedString(dodgeEvent.c_str()));
        const auto &fallback = ::CombatAI::Config::GetInstance().GetDodgeSystem().eventFallback;
        if (!success && !fallback.empty() && fallback != dodgeEvent) {
            success = a_actor->NotifyAnimationGraph(RE::BSFixedString(fallback.c_str()));
            dodgeEvent = fallback;
        }
        if (::CombatAI::Config::GetInstance().IsDebugTarget(a_actor->GetFormID())) {
            LOG_DEBUG("Dodge 0x{:08X}: event {} {}", a_actor->GetFormID(), dodgeEvent, success ? "accepted" : "rejected");
        }

        // Apply stamina cost if dodge was successful
        // This matches TK Dodge RE's behavior (applies cost when dodge starts)
        if (success) {
            ApplyDodgeStaminaCost(a_actor);
        }

        return success;
    }

    bool DodgeSystem::ExecuteEvasionDodge(RE::Actor *a_actor, const ActorStateData &a_state)
    {
        if (!a_actor || !a_state.target.isValid) {
            return false;
        }

        // Calculate dodge direction (perpendicular to target, away from danger)
        // For evasion, we want to dodge perpendicular to the line between self and target
        RE::NiPoint3 toTarget = a_state.target.position - a_state.self.position;
        toTarget.z = 0.0f; // Keep on horizontal plane
        toTarget.Unitize();

        // Calculate perpendicular direction (90 degrees)
        // Choose the direction that moves away from target's forward vector (if target is attacking)
        RE::NiPoint3 dodgeDir;

        if (a_state.target.isAttacking || a_state.target.isPowerAttacking) {
            // If target is attacking, dodge perpendicular to their attack direction
            // Use target's forward vector to determine best dodge direction
            RE::NiPoint3 targetForward = a_state.target.forwardVector;
            targetForward.z = 0.0f;
            targetForward.Unitize();

            // Perpendicular: rotate 90 degrees
            dodgeDir = RE::NiPoint3(-targetForward.y, targetForward.x, 0.0f);
        } else {
            // If target is just blocking, dodge perpendicular to the line between us
            dodgeDir = RE::NiPoint3(-toTarget.y, toTarget.x, 0.0f);
        }

        dodgeDir.Unitize();

        // Cliff-safe evasion: if the chosen side is a ledge, try the mirror side before
        // committing. A sideways evasion is equally valid left or right, so prefer the
        // one with ground. If both sides drop off, ExecuteDodge's guard aborts.
        const auto &envCfg = ::CombatAI::Config::GetInstance().GetEnvSensor();
        if (envCfg.enableCliffSafeDodge) {
            RE::NiPoint3 flat = dodgeDir;
            flat.z = 0.0f;
            flat.Unitize();
            if (!EnvSensor::HasGroundAhead(a_actor, flat, envCfg.dodgeLedgeProbeDistance, envCfg.dodgeMaxDrop)) {
                RE::NiPoint3 mirror(-flat.x, -flat.y, 0.0f);
                if (EnvSensor::HasGroundAhead(a_actor, mirror, envCfg.dodgeLedgeProbeDistance, envCfg.dodgeMaxDrop)) {
                    dodgeDir = mirror;
                }
            }
        }

        return ExecuteDodge(a_actor, dodgeDir);
    }

    std::string DodgeSystem::DetermineDodgeDirection(RE::Actor *a_actor, const RE::NiPoint3 &a_dodgeDirection)
    {
        const auto &events = ::CombatAI::Config::GetInstance().GetDodgeSystem();
        if (!a_actor) {
            return events.eventBack;
        }

        // Get actor's forward vector
        RE::NiPoint3 actorForward = StateHelpers::GetActorForwardVector(a_actor);

        // Normalize dodge direction
        RE::NiPoint3 normalizedDodge = a_dodgeDirection;
        normalizedDodge.z = 0.0f;
        normalizedDodge.Unitize();

        // Calculate angle between actor forward and dodge direction
        // Using 2D vectors for angle calculation
        RE::NiPoint2 forward2D(actorForward.x, actorForward.y);
        RE::NiPoint2 dodge2D(normalizedDodge.x, normalizedDodge.y);

        // Calculate angle using atan2
        float forwardAngle = std::atan2(forward2D.y, forward2D.x);
        float dodgeAngle = std::atan2(dodge2D.y, dodge2D.x);
        float angleDiff = dodgeAngle - forwardAngle;

        // Normalize angle to [-PI, PI]
        while (angleDiff > PI)
            angleDiff -= 2.0f * PI;
        while (angleDiff < -PI)
            angleDiff += 2.0f * PI;

        // Determine dodge direction based on angle
        // Forward: -PI/4 to PI/4
        // Right: PI/4 to 3*PI/4
        // Back: 3*PI/4 to -3*PI/4 (or PI to -PI)
        // Left: -3*PI/4 to -PI/4

        if (angleDiff >= -2.0f * PI8 && angleDiff < 2.0f * PI8) {
            return events.eventForward;
        } else if (angleDiff >= 2.0f * PI8 && angleDiff < 6.0f * PI8) {
            return events.eventRight;
        } else if (angleDiff >= -6.0f * PI8 && angleDiff < -2.0f * PI8) {
            return events.eventLeft;
        } else {
            return events.eventBack;
        }
    }

    bool DodgeSystem::IsDodging(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return false;
        }

        bool isDodging = false;
        a_actor->GetGraphVariableBool(RE::BSFixedString("bIsDodging"), isDodging);
        return isDodging;
    }

    void DodgeSystem::ApplyDodgeStaminaCost(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return;
        }

        // Apply stamina cost using DamageActorValue
        // In CommonLibSSE, DamageActorValue takes 2 arguments: (ActorValue, float)
        // This matches TK Dodge RE's behavior (reduces stamina when dodge starts)
        auto actorValueOwner = ActorUtils::SafeAsActorValueOwner(a_actor);
        if (actorValueOwner) {
            // Use DamageActorValue to reduce stamina (positive value = damage)
            actorValueOwner->DamageActorValue(RE::ActorValue::kStamina, m_config.dodgeStaminaCost);
        }
    }
} // namespace CombatAI
