#include "DodgeSystem.h"
#include "ActorUtils.h"
#include "Config.h"
#include "EnvSensor.h"
#include "pch.h"
#include <cmath>
#include <random>

namespace CombatAI
{
    namespace
    {
        constexpr float PI = 3.14159265f;
        constexpr float PI8 = 0.39269908f; // PI / 8
        // Path probe: TK Dodge moved NPCs 150-340 units (run 26); the ray starts outside the body and runs at
        // waist height, above small rocks and steps
        constexpr float kPathStart = 35.0f;
        constexpr float kPathLength = 220.0f;
        constexpr float kPathHeight = 60.0f;
        // Side rays this far left and right of the centre line catch a pillar or door frame that would clip a
        // shoulder (NPC body radius is ~24)
        constexpr float kShoulderOffset = 22.0f;
    } // namespace

    const char *DodgeSystem::Blocked(RE::Actor *a_actor, const RE::NiPoint3 &a_dir, const RE::TESObjectREFR *a_ignore)
    {
        const auto &envCfg = ::CombatAI::Config::GetInstance().GetEnvSensor();
        if (envCfg.enableCliffSafeDodge &&
            !EnvSensor::HasGroundAhead(a_actor, a_dir, envCfg.dodgeLedgeProbeDistance, envCfg.dodgeMaxDrop)) {
            return "ledge";
        }
        const auto pos = ActorUtils::SafeGetPosition(a_actor);
        if (!pos) {
            return nullptr; // fail open, like the ledge probe
        }
        // Centre line, then both shoulders; the target the dodge closes on doesn't count (run 30: 17 forward
        // dodges blocked by it)
        const RE::NiPoint3 side(-a_dir.y, a_dir.x, 0.0f);
        for (const float offset : {0.0f, kShoulderOffset, -kShoulderOffset}) {
            RE::NiPoint3 from = *pos + a_dir * kPathStart + side * offset;
            RE::NiPoint3 to = *pos + a_dir * kPathLength + side * offset;
            from.z += kPathHeight;
            to.z += kPathHeight;
            const auto hit = EnvSensor::CastRay(a_actor, from, to);
            if (!hit.hit || (a_ignore && hit.ref == a_ignore)) {
                continue;
            }
            const bool actor = hit.ref && hit.ref->As<RE::Actor>();
            if (offset == 0.0f) {
                return actor ? "actor" : "obstacle";
            }
            return actor ? "actor at shoulder" : "obstacle at shoulder";
        }
        return nullptr;
    }

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

    bool DodgeSystem::ExecuteDodge(RE::Actor *a_actor, std::initializer_list<RE::NiPoint3> a_options,
                                   const RE::TESObjectREFR *a_ignore)
    {
        if (!a_actor || !CanDodge(a_actor)) {
            return false;
        }
        const bool debug = ::CombatAI::Config::GetInstance().IsDebugTarget(a_actor->GetFormID());

        // First clear option wins: a ledge, wall, object or actor in the way only bumps or drops the actor
        std::string dodgeEvent;
        std::string skipped;
        for (RE::NiPoint3 dir : a_options) {
            dir.z = 0.0f;
            if (dir.Unitize() < 0.01f) {
                continue;
            }
            const std::string event = DetermineDodgeDirection(a_actor, dir);
            if (const char *why = Blocked(a_actor, dir, a_ignore)) {
                if (debug) {
                    skipped += std::format("{} blocked ({}), ", event, why);
                }
                continue;
            }
            dodgeEvent = event;
            break;
        }
        if (dodgeEvent.empty()) {
            if (debug) {
                LOG_DEBUG("Dodge 0x{:08X}: no clear direction ({})", a_actor->GetFormID(), skipped);
            }
            return false;
        }

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
        if (debug) {
            // Facing is logged so the analysis can check the event against the actual movement
            const RE::NiPoint3 fwd = StateHelpers::GetActorForwardVector(a_actor);
            LOG_DEBUG("Dodge 0x{:08X}: event {} {} (facing {:.2f},{:.2f}){}{}", a_actor->GetFormID(), dodgeEvent,
                      success ? "accepted" : "rejected", fwd.x, fwd.y, skipped.empty() ? "" : "; ", skipped);
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
            // Out of the swing's line: perpendicular to the attacker's facing
            RE::NiPoint3 targetForward = a_state.target.forwardVector;
            targetForward.z = 0.0f;
            targetForward.Unitize();
            dodgeDir = RE::NiPoint3(-targetForward.y, targetForward.x, 0.0f);
        } else {
            dodgeDir = RE::NiPoint3(-toTarget.y, toTarget.x, 0.0f);
        }

        // Either side clears the swing: pick one at random (it was always the attacker's left), the other if
        // that one is blocked, then straight back
        thread_local std::mt19937 rng{std::random_device{}()};
        if (rng() & 1u) {
            dodgeDir = -dodgeDir;
        }
        return ExecuteDodge(a_actor, {dodgeDir, -dodgeDir, -toTarget});
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

        // Skyrim's heading turns clockwise (forward = (sin h, cos h)) while atan2 counts counter-clockwise,
        // so a positive angle is to the actor's LEFT. Before 0.13.0 left and right were swapped here.
        // Forward: -PI/4 to PI/4, Left: PI/4 to 3*PI/4, Right: -3*PI/4 to -PI/4, Back: the rest
        if (angleDiff >= -2.0f * PI8 && angleDiff < 2.0f * PI8) {
            return events.eventForward;
        } else if (angleDiff >= 2.0f * PI8 && angleDiff < 6.0f * PI8) {
            return events.eventLeft;
        } else if (angleDiff >= -6.0f * PI8 && angleDiff < -2.0f * PI8) {
            return events.eventRight;
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
