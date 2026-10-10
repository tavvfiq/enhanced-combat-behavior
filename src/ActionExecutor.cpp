#include "ActionExecutor.h"
#include "ActorUtils.h"
#include "Config.h"
#include "GuardCounterFeedbackTracker.h"
#include "ParryFeedbackTracker.h"
#include "TimedBlockFeedbackTracker.h"
#include "TimedBlockIntegration.h"
#include "pch.h"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace CombatAI
{
    void ActionExecutor::EnableBFCO(bool isEnabled)
    {
        if (isEnabled) {
            LOG_INFO("BFCO integration enabled");
        } else {
            LOG_INFO("integration with BFCO disabled");
        }

        m_isBFCOEnabled = isEnabled;
    }

    bool ActionExecutor::Execute(RE::Actor *a_actor, const DecisionResult &a_decision, const ActorStateData &a_state)
    {
        if (!a_actor || a_decision.action == ActionType::None) {
            return false;
        }

        const bool targetsHiddenPlayer = a_state.target.isPlayer && !a_state.target.isDetected;
        if (targetsHiddenPlayer &&
            (a_decision.action == ActionType::Attack || a_decision.action == ActionType::PowerAttack ||
             a_decision.action == ActionType::SprintAttack || a_decision.action == ActionType::Advancing ||
             a_decision.action == ActionType::ForwardDodge)) {
            return false;
        }

        // Reset jump variable if executing a different action
        // This prevents the jump variable from staying true when we want to do something else
        if (a_decision.action != ActionType::Jump) {
            ResetJumpVariable(a_actor);
        }

        // Don't interrupt a block for movement actions that have no movement backend.
        // Their executors return false when CPR cannot handle the action, so stopping
        // the block first only lets vanilla combat AI immediately start blocking again.
        bool movementCanRun = true;
        switch (a_decision.action) {
        case ActionType::Strafe:
        case ActionType::Flanking:
            movementCanRun = a_state.target.isValid && IsCPRAvailable(a_actor) && IsMeleeOnlyActor(a_actor);
            break;
        case ActionType::Retreat:
        case ActionType::Backoff:
            movementCanRun = IsCPRAvailable(a_actor);
            break;
        case ActionType::Advancing: {
            auto &config = Config::GetInstance();
            const bool meleeWithTarget = IsMeleeOnlyActor(a_actor) && a_state.target.isValid;
            movementCanRun = (IsCPRAvailable(a_actor) && meleeWithTarget) ||
                             (config.GetDecisionMatrix().enableSprintCharge && meleeWithTarget);
            break;
        }
        case ActionType::ForwardDodge:
            movementCanRun = a_state.target.isValid;
            break;
        default:
            break;
        }

        bool blockReleased = false;
        if (a_state.self.isBlocking && movementCanRun) {
            switch (a_decision.action) {
            case ActionType::Bash:
            case ActionType::Parry:
            case ActionType::TimedBlock:
                break; // these rely on the block state
            default:
                blockReleased = NotifyAnimation(a_actor, "blockStop");
                break;
            }
        }

        // Sprint-charge (from advancing) must stop before a standing attack so the NPC
        // plants to swing. SprintAttack manages its own sprint; leave it alone.
        switch (a_decision.action) {
        case ActionType::Attack:
        case ActionType::PowerAttack:
        case ActionType::Bash:
        case ActionType::Parry:
        case ActionType::TimedBlock:
        case ActionType::ForwardDodge:
            if (ActorUtils::SafeIsSprinting(a_actor)) {
                ActorUtils::SafeSetSprinting(a_actor, false);
            }
            break;
        default:
            break;
        }

        bool success = false;

        switch (a_decision.action) {
        case ActionType::Attack:
            success = ExecuteAttack(a_actor);
            break;

        case ActionType::PowerAttack:
            success = ExecutePowerAttack(a_actor, a_state);
            break;

        case ActionType::SprintAttack:
            success = ExecuteSprintAttack(a_actor);
            break;

        case ActionType::Bash:
            success = ExecuteBash(a_actor, &a_state);
            break;

        case ActionType::Parry:
            success = ExecuteParry(a_actor, a_state);
            break;

        case ActionType::TimedBlock:
            success = ExecuteTimedBlock(a_actor, a_state);
            break;

        case ActionType::Strafe:
            success = ExecuteStrafe(a_actor, a_decision, a_state);
            break;

        case ActionType::Flanking:
            success = ExecuteFlanking(a_actor, a_decision, a_state);
            break;

        case ActionType::Retreat:
            success = ExecuteRetreat(a_actor, a_decision, a_state);
            break;

        case ActionType::Jump:
            success = ExecuteJump(a_actor, a_state);
            break;

        case ActionType::Dodge:
            success = ExecuteDodge(a_actor, a_state);
            break;

        case ActionType::ForwardDodge:
            success = ExecuteForwardDodge(a_actor, a_decision, a_state);
            break;

        case ActionType::Backoff:
            success = ExecuteBackoff(a_actor, a_decision, a_state);
            break;

        case ActionType::Advancing:
            success = ExecuteAdvancing(a_actor, a_decision, a_state);
            break;

        case ActionType::Feint:
            success = ExecuteFeint(a_actor, a_decision, a_state);
            break;

        default:
            return false;
        }

        // Some actions can still fail after their preconditions pass (for example,
        // the actor can enter an attack state between the state snapshot and execute).
        // Restore blocking when that happens so a failed action doesn't leave the NPC
        // exposed until vanilla AI starts its block again.
        if (!success && blockReleased) {
            NotifyAnimation(a_actor, "blockStart");
        }

        return success;
    }

    bool ActionExecutor::ExecuteBash(RE::Actor *a_actor, const ActorStateData *a_state)
    {
        if (!a_actor) {
            return false;
        }

        // Check if already bashing/attacking (don't interrupt)
        RE::ATTACK_STATE_ENUM attackState = ActorUtils::SafeGetAttackState(a_actor);
        if (attackState != RE::ATTACK_STATE_ENUM::kNone && attackState != RE::ATTACK_STATE_ENUM::kDraw) {
            // Already in an attack, don't interrupt
            return false;
        }

        // Reset BFCO attack state if BFCO is installed
        if (m_isBFCOEnabled) {
            ResetBFCOAttackState(a_actor);
        }

        // Trigger bash animation (regular bash, not parry)
        return NotifyAnimation(a_actor, "bashStart");
    }

    bool ActionExecutor::ExecuteParry(RE::Actor *a_actor, const ActorStateData &a_state)
    {
        if (!a_actor) {
            return false;
        }

        // Check if already bashing/attacking (don't interrupt)
        RE::ATTACK_STATE_ENUM attackState = ActorUtils::SafeGetAttackState(a_actor);
        if (attackState != RE::ATTACK_STATE_ENUM::kNone && attackState != RE::ATTACK_STATE_ENUM::kDraw) {
            // Already in an attack, don't interrupt
            return false;
        }

        // Reset BFCO attack state if BFCO is installed
        if (m_isBFCOEnabled) {
            ResetBFCOAttackState(a_actor);
        }

        // Always record parry attempt - this is specifically a parry action
        if (a_state.target.isValid && (a_state.target.isAttacking || a_state.target.isPowerAttacking) &&
            a_state.temporal.target.timeUntilAttackHits < 999.0f) {
            // Get target actor
            RE::Actor *target = nullptr;
            try {
                RE::CombatController *combatController = ActorUtils::SafeGetCombatController(a_actor);
                if (combatController) {
                    RE::ActorHandle targetHandle = combatController->targetHandle;
                    RE::NiPointer<RE::Actor> targetPtr = targetHandle.get();
                    if (targetPtr && targetPtr.get()) {
                        target = targetPtr.get();
                    }
                }
            } catch (...) {
                // Target access failed
            }

            if (target) {
                ParryFeedbackTracker::GetInstance().RecordParryAttempt(a_actor, target,
                                                                       a_state.temporal.target.estimatedAttackDuration,
                                                                       a_state.temporal.target.timeUntilAttackHits);
            }
        }

        // Trigger bash animation for parry
        // EldenParry mod will automatically detect this via animation events and handle
        // the parry timing. The decision matrix already evaluated the timing window,
        // so this bash is timed correctly for parrying.
        return NotifyAnimation(a_actor, "bashStart");
    }

    bool ActionExecutor::ExecuteTimedBlock(RE::Actor *a_actor, const ActorStateData &a_state)
    {
        if (!a_actor) {
            return false;
        }

        // Check if already blocking/attacking (don't interrupt)
        RE::ATTACK_STATE_ENUM attackState = ActorUtils::SafeGetAttackState(a_actor);
        if (attackState != RE::ATTACK_STATE_ENUM::kNone && attackState != RE::ATTACK_STATE_ENUM::kDraw) {
            // Already in an attack, don't interrupt
            return false;
        }

        // Reset BFCO attack state if BFCO is installed
        if (m_isBFCOEnabled) {
            ResetBFCOAttackState(a_actor);
        }

        // Apply timed block window spell (creates 0.33s window for timed block)
        // This must be applied before blocking starts
        if (!TimedBlockIntegration::GetInstance().ApplyTimedBlockWindow(a_actor)) {
            LOG_WARN("Failed to apply timed block window spell");
            return false;
        }

        // Record timed block attempt for feedback tracking
        if (a_state.target.isValid && (a_state.target.isAttacking || a_state.target.isPowerAttacking) &&
            a_state.temporal.target.timeUntilAttackHits < 999.0f) {
            // Get target actor
            RE::Actor *target = nullptr;
            try {
                RE::CombatController *combatController = ActorUtils::SafeGetCombatController(a_actor);
                if (combatController) {
                    RE::ActorHandle targetHandle = combatController->targetHandle;
                    RE::NiPointer<RE::Actor> targetPtr = targetHandle.get();
                    if (targetPtr && targetPtr.get()) {
                        target = targetPtr.get();
                    }
                }
            } catch (...) {
                // Target access failed
            }

            if (target) {
                TimedBlockFeedbackTracker::GetInstance().RecordTimedBlockAttempt(
                    a_actor, target, a_state.temporal.target.estimatedAttackDuration,
                    a_state.temporal.target.timeUntilAttackHits);
            }
        }

        // Trigger block animation/state
        // Simple Timed Block mod will automatically detect hits during the window
        // and handle the timed block success (damage prevention, stagger, etc.)
        return NotifyAnimation(a_actor, "blockStart");
    }

    bool ActionExecutor::ExecuteDodge(RE::Actor *a_actor, const ActorStateData &a_state)
    {
        if (!a_actor) {
            return false;
        }

        ConfigureDodgeSystem();
        return m_dodgeSystem.ExecuteEvasionDodge(a_actor, a_state);
    }

    bool ActionExecutor::ExecuteForwardDodge(RE::Actor *a_actor, const DecisionResult &a_decision,
                                             const ActorStateData &a_state)
    {
        if (!a_actor || !a_state.target.isValid) {
            return false;
        }

        RE::NiPoint3 direction = a_decision.direction;
        direction.z = 0.0f;
        if (direction.x == 0.0f && direction.y == 0.0f) {
            direction = a_state.target.position - a_state.self.position;
            direction.z = 0.0f;
        }
        if (direction.x == 0.0f && direction.y == 0.0f) {
            return false;
        }
        direction.Unitize();

        ConfigureDodgeSystem();
        return m_dodgeSystem.ExecuteDodge(a_actor, direction);
    }

    void ActionExecutor::ConfigureDodgeSystem()
    {
        auto &config = Config::GetInstance();
        DodgeSystem::Config dodgeConfig;
        dodgeConfig.dodgeStaminaCost = config.GetDodgeSystem().dodgeStaminaCost;
        dodgeConfig.iFrameDuration = config.GetDodgeSystem().iFrameDuration;
        dodgeConfig.enableStepDodge = config.GetDodgeSystem().enableStepDodge;
        dodgeConfig.enableDodgeAttackCancel = config.GetDodgeSystem().enableDodgeAttackCancel;
        m_dodgeSystem.SetConfig(dodgeConfig);
    }

    bool ActionExecutor::ExecuteStrafe(RE::Actor *a_actor, const DecisionResult &a_decision,
                                       const ActorStateData &a_state)
    {
        if (!a_actor) {
            return false;
        }

        // Validate target before accessing target state
        if (!a_state.target.isValid) {
            return false; // Can't strafe without a valid target
        }

        // Get movement direction from decision (could be normal strafe or flanking)
        RE::NiPoint3 movementDir = a_decision.direction;

        // Try CPR circling if available
        // NOTE: CPR only works for melee-only actors (no ranged weapons or magic)
        // If actor has ranged/magic, CPR won't work, so we fall back to direct movement
        bool isMeleeOnly = IsMeleeOnlyActor(a_actor);
        if (IsCPRAvailable(a_actor) && isMeleeOnly) {
            // Calculate circling parameters based on distance and direction
            // Make ranges more aggressive to ensure circling triggers
            float minDist = (std::max)(50.0f, a_state.target.distance * 0.7f); // More aggressive min distance
            float maxDist = a_state.target.distance * 1.3f;                    // Wider max distance for better circling

            // Calculate circling angles based on strafe direction
            // CPR angles are relative to target's facing direction (0° = front, 90° = side, 180° = back)
            float minAngle, maxAngle;
            CalculateCPRCirclingAngles(movementDir, a_state, minAngle, maxAngle);

            SetCPRCircling(a_actor, minDist, maxDist, minAngle, maxAngle);

            // Set movement direction to guide CPR circling (works for both normal strafe and flanking)
            if (movementDir.x != 0.0f || movementDir.y != 0.0f) {
                SetMovementDirection(a_actor, movementDir, a_decision.intensity);
            }
            return true;
        }

        // No CPR available, or CPR only works for melee-only actors: the direct velocity
        // nudge fights Skyrim's own combat AI and produces jittery, unpathed movement.
        // Let the native combat AI handle positioning instead.
        return false;
    }

    bool ActionExecutor::ExecuteFlanking(RE::Actor *a_actor, const DecisionResult &a_decision,
                                         const ActorStateData &a_state)
    {
        if (!a_actor) {
            return false;
        }

        // Validate target before accessing target state
        if (!a_state.target.isValid) {
            return false; // Can't flank without a valid target
        }

        // Flanking is similar to strafe but with tactical positioning
        // Use the calculated flanking direction from decision (perpendicular to target, away from ally)
        RE::NiPoint3 flankDir = a_decision.direction;

        // Try CPR circling if available and actor is melee-only
        bool isMeleeOnly = IsMeleeOnlyActor(a_actor);
        if (IsCPRAvailable(a_actor) && isMeleeOnly) {
            // Calculate circling parameters for flanking movement
            float minDist = (std::max)(50.0f, a_state.target.distance * 0.7f);
            float maxDist = a_state.target.distance * 1.3f;

            // Calculate circling angles based on flanking direction
            float minAngle, maxAngle;
            CalculateCPRCirclingAngles(flankDir, a_state, minAngle, maxAngle);

            SetCPRCircling(a_actor, minDist, maxDist, minAngle, maxAngle);

            // Apply flanking direction to guide CPR circling (creates pincer movement)
            if (flankDir.x != 0.0f || flankDir.y != 0.0f) {
                SetMovementDirection(a_actor, flankDir, a_decision.intensity);
            }
            return true;
        }

        // No CPR available, or CPR only works for melee-only actors: let the native
        // combat AI handle positioning instead of nudging with raw velocity.
        return false;
    }

    bool ActionExecutor::ExecuteRetreat(RE::Actor *a_actor, [[maybe_unused]] const DecisionResult &a_decision,
                                        const ActorStateData &a_state)
    {
        if (!a_actor) {
            return false;
        }

        // Try CPR fallback if available
        // NOTE: CPR fallback works for all actors (not just melee-only)
        if (IsCPRAvailable(a_actor)) {
            // Calculate fallback parameters
            float retreatDistance =
                a_state.target.isValid ? a_state.target.distance : 600.0f; // Default if no valid target
            float minDist = retreatDistance;
            float maxDist = retreatDistance * 1.5f;
            float minWait = 1.5f; // TODO: make configurable
            float maxWait = 3.0f; // TODO: make configurable

            SetCPRFallback(a_actor, minDist, maxDist, minWait, maxWait);
            return true;
        }

        // No CPR: let the native combat AI handle retreat instead of nudging with raw velocity.
        return false;
    }

    bool ActionExecutor::ExecuteSprintAttack(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return false;
        }

        // Start sprinting if not already
        if (!ActorUtils::SafeIsSprinting(a_actor)) {
            NotifyAnimation(a_actor, "SprintStart");
            ActorUtils::SafeSetSprinting(a_actor, true);
        }

        // Reset BFCO attack state if BFCO is installed
        if (m_isBFCOEnabled) {
            ResetBFCOAttackState(a_actor);
        }

        // Trigger sprint attack
        // BFCO supports attackStartSprint, vanilla also supports it
        NotifyAnimation(a_actor, "attackStartSprint");
        NotifyAnimation(a_actor, "attackStart");

        return true;
    }

    bool ActionExecutor::NotifyAnimation(RE::Actor *a_actor, const char *a_eventName)
    {
        if (!a_actor) {
            return false;
        }
        return ActorUtils::SafeNotifyAnimationGraph(a_actor, a_eventName);
    }

    bool ActionExecutor::SetMovementDirection(RE::Actor *a_actor, const RE::NiPoint3 &a_direction, float a_intensity)
    {
        if (!a_actor) {
            return false;
        }

        RE::NiAVObject *actor3d = ActorUtils::SafeGet3D(a_actor);
        if (!actor3d) {
            return false;
        }

        RE::NiPoint3 worldDir = a_direction;
        worldDir.Unitize();

        // Get actor's rotation matrix (transpose is the inverse for rotation matrices)
        RE::NiMatrix3 invActorRot = actor3d->world.rotate.Transpose();

        // Transform world direction to local direction
        RE::NiPoint3 localDir = invActorRot * worldDir;
        localDir.Unitize();

        // Calculate angle (Radians) and Speed (Normalized 0-1)
        float angleRad = atan2(localDir.x, localDir.y);
        float speedNorm = std::min(a_intensity, 1.0f);

        // Convert to Degrees for Animation Graph (-180 to 180)
        constexpr float R2D = 57.2957795131f; // 180 / PI
        float angleDeg = angleRad * R2D;

        // Set Standard/Mod scale variables
        // Vanilla 'Speed' is usually ~0-400 (Walk ~80, Run ~370)
        // We set it to trigger the transition from Idle -> Locomotion
        float animSpeed = speedNorm * 370.0f; // Default to Run speed at max intensity

        // Vanilla Variables
        ActorUtils::SafeSetGraphVariableFloat(a_actor, "Speed", animSpeed);
        ActorUtils::SafeSetGraphVariableFloat(a_actor, "Direction", angleDeg);

        // Modern Movement / Mod Variables
        ActorUtils::SafeSetGraphVariableFloat(a_actor, "movementDirection", angleDeg); // Usually expects Degrees
        ActorUtils::SafeSetGraphVariableFloat(a_actor, "movementSpeed", speedNorm);

        // InputDirection/Magnitude (TDM/MCO often use these)
        ActorUtils::SafeSetGraphVariableFloat(a_actor, "InputDirection", angleDeg); // TDM expects Degrees
        ActorUtils::SafeSetGraphVariableFloat(a_actor, "InputMagnitude", speedNorm);

        // Direct Physics Control
        // Apply velocity (this overrides vanilla pathfinding velocity for this frame)
        // Physics Speed needs to match Animation Speed to look natural
        float physicsSpeed = 300.0f * speedNorm;
        RE::NiPoint3 velocity = worldDir * physicsSpeed;

        ActorUtils::SafeApplyVelocity(a_actor, velocity);

        return true;
    }

    bool ActionExecutor::IsCPRAvailable(RE::Actor *a_actor) const
    {
        if (!a_actor) {
            return false;
        }

        // Check config first
        auto &config = Config::GetInstance();
        if (!config.GetModIntegrations().enableCPRIntegration) {
            return false;
        }

        // Check if CPR graph variables are available by trying to read one
        // If the variable exists, CPR is likely available
        bool enableCircling = false;
        return ActorUtils::SafeGetGraphVariableBool(a_actor, "CPR_EnableCircling", enableCircling);
    }

    bool ActionExecutor::IsMeleeOnlyActor(RE::Actor *a_actor) const
    {
        if (!a_actor) {
            return false;
        }

        // Check if actor has ranged weapon or magic equipped
        // CPR only works for melee-only actors
        // Wrap all checks in try-catch to prevent crashes from stale form pointers
        try {
            auto rightHand = ActorUtils::SafeGetEquippedObject(a_actor, false);

            // Check right hand for ranged weapon
            if (rightHand) {
                try {
                    if (rightHand->IsWeapon()) {
                        auto weapon = rightHand->As<RE::TESObjectWEAP>();
                        if (weapon) {
                            try {
                                if (weapon->IsBow() || weapon->IsCrossbow()) {
                                    return false; // Has ranged weapon
                                }
                            } catch (...) {
                                // Weapon access failed - assume melee to be safe
                            }
                        }
                    }
                } catch (...) {
                    // Form access failed - assume melee to be safe
                }
            }

            // Check if actor is currently casting magic
            // This is more reliable than checking left hand (which could be shield)
            if (ActorUtils::SafeWhoIsCasting(a_actor) != 0) {
                return false; // Actor is casting magic
            }

            return true; // Melee-only actor
        } catch (...) {
            // Actor access failed - return false to be safe (skip CPR)
            return false;
        }
    }

    void ActionExecutor::ResetBFCOAttackState(RE::Actor *a_actor)
    {
        if (!a_actor || !m_isBFCOEnabled) {
            return;
        }

        // Reset BFCO attack state variables
        ActorUtils::SafeSetGraphVariableInt(a_actor, "NEW_BFCO_IsNormalAttacking", 0);
        ActorUtils::SafeSetGraphVariableInt(a_actor, "NEW_BFCO_IsPowerAttacking", 0);
        ActorUtils::SafeSetGraphVariableInt(a_actor, "BFCONG_PARMB", 0);
        ActorUtils::SafeSetGraphVariableInt(a_actor, "NEW_BFCO_DisablePALMB", 0);
    }

    void ActionExecutor::SetCPRCircling(RE::Actor *a_actor, float a_minDist, float a_maxDist, float a_minAngle,
                                        float a_maxAngle)
    {
        if (!a_actor) {
            return;
        }

        // IMPORTANT: Disable other CPR behaviors first to avoid conflicts
        ActorUtils::SafeSetGraphVariableBool(a_actor, "CPR_EnableFallback", false);
        ActorUtils::SafeSetGraphVariableBool(a_actor, "CPR_EnableAdvanceRadius", false);
        ActorUtils::SafeSetGraphVariableBool(a_actor, "CPR_EnableBackoff", false);

        // Enable CPR circling
        ActorUtils::SafeSetGraphVariableBool(a_actor, "CPR_EnableCircling", true);
        ActorUtils::SafeSetGraphVariableFloat(a_actor, "CPR_CirclingDistMin", a_minDist);
        ActorUtils::SafeSetGraphVariableFloat(a_actor, "CPR_CirclingDistMax", a_maxDist);
        ActorUtils::SafeSetGraphVariableFloat(a_actor, "CPR_CirclingAngleMin", a_minAngle);
        ActorUtils::SafeSetGraphVariableFloat(a_actor, "CPR_CirclingAngleMax", a_maxAngle);
        ActorUtils::SafeSetGraphVariableFloat(a_actor, "CPR_CirclingViewConeAngle", 360.0f); // Full view cone
    }

    void ActionExecutor::SetCPRFallback(RE::Actor *a_actor, float a_minDist, float a_maxDist, float a_minWait,
                                        float a_maxWait)
    {
        if (!a_actor) {
            return;
        }

        // IMPORTANT: Disable other CPR behaviors first to avoid conflicts
        ActorUtils::SafeSetGraphVariableBool(a_actor, "CPR_EnableCircling", false);
        ActorUtils::SafeSetGraphVariableBool(a_actor, "CPR_EnableAdvanceRadius", false);
        ActorUtils::SafeSetGraphVariableBool(a_actor, "CPR_EnableBackoff", false);

        // Enable CPR fallback
        ActorUtils::SafeSetGraphVariableBool(a_actor, "CPR_EnableFallback", true);
        ActorUtils::SafeSetGraphVariableFloat(a_actor, "CPR_FallbackDistMin", a_minDist);
        ActorUtils::SafeSetGraphVariableFloat(a_actor, "CPR_FallbackDistMax", a_maxDist);
        // Reduce wait times to make fallback more responsive (less waiting around)
        ActorUtils::SafeSetGraphVariableFloat(a_actor, "CPR_FallbackWaitTimeMin", a_minWait * 0.5f); // Faster recovery
        ActorUtils::SafeSetGraphVariableFloat(a_actor, "CPR_FallbackWaitTimeMax", a_maxWait * 0.5f); // Faster recovery
    }

    void ActionExecutor::SetCPRBackoff(RE::Actor *a_actor, float a_minDistMult)
    {
        if (!a_actor) {
            return;
        }

        // IMPORTANT: Disable other CPR behaviors first to avoid conflicts
        ActorUtils::SafeSetGraphVariableBool(a_actor, "CPR_EnableCircling", false);
        ActorUtils::SafeSetGraphVariableBool(a_actor, "CPR_EnableAdvanceRadius", false);
        ActorUtils::SafeSetGraphVariableBool(a_actor, "CPR_EnableFallback", false);

        // Enable CPR backoff
        ActorUtils::SafeSetGraphVariableBool(a_actor, "CPR_EnableBackoff", true);
        ActorUtils::SafeSetGraphVariableFloat(a_actor, "CPR_BackoffMinDistMult", a_minDistMult);
        ActorUtils::SafeSetGraphVariableFloat(a_actor, "CPR_BackoffChance", 1.0f); // Always backoff when triggered
    }

    void ActionExecutor::DisableCPR(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return;
        }

        // Disable all CPR behaviors
        ActorUtils::SafeSetGraphVariableBool(a_actor, "CPR_EnableCircling", false);
        ActorUtils::SafeSetGraphVariableBool(a_actor, "CPR_EnableAdvanceRadius", false);
        ActorUtils::SafeSetGraphVariableBool(a_actor, "CPR_EnableBackoff", false);
        ActorUtils::SafeSetGraphVariableBool(a_actor, "CPR_EnableFallback", false);
    }

    bool ActionExecutor::ExecuteAttack(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return false;
        }

        // Check if already attacking
        RE::ATTACK_STATE_ENUM attackState = ActorUtils::SafeGetAttackState(a_actor);
        if (attackState != RE::ATTACK_STATE_ENUM::kNone && attackState != RE::ATTACK_STATE_ENUM::kDraw) {
            return false;
        }

        if (m_isBFCOEnabled) {
            ActorUtils::SafeSetGraphVariableInt(a_actor, "NEW_BFCO_IsNormalAttacking", 1);
            ActorUtils::SafeSetGraphVariableInt(a_actor, "NEW_BFCO_IsPowerAttacking", 0);
        }

        return NotifyAnimation(a_actor, "attackStart");
    }

    bool ActionExecutor::ExecutePowerAttack(RE::Actor *a_actor, const ActorStateData &a_state)
    {
        if (!a_actor) {
            return false;
        }

        // Check if already attacking
        RE::ATTACK_STATE_ENUM attackState = ActorUtils::SafeGetAttackState(a_actor);
        if (attackState != RE::ATTACK_STATE_ENUM::kNone && attackState != RE::ATTACK_STATE_ENUM::kDraw) {
            return false;
        }

        // Track guard counter attempt if guard counter window is active
        if (a_state.self.isGuardCounterActive && a_state.target.isValid) {
            // Get target actor
            RE::Actor *target = nullptr;
            try {
                RE::CombatController *combatController = ActorUtils::SafeGetCombatController(a_actor);
                if (combatController) {
                    RE::ActorHandle targetHandle = combatController->targetHandle;
                    RE::NiPointer<RE::Actor> targetPtr = targetHandle.get();
                    if (targetPtr && targetPtr.get()) {
                        target = targetPtr.get();
                    }
                }
            } catch (...) {
                // Target access failed
            }

            if (target) {
                GuardCounterFeedbackTracker::GetInstance().RecordGuardCounterAttempt(a_actor, target);
            }
        }

        if (m_isBFCOEnabled) {
            ActorUtils::SafeSetGraphVariableInt(a_actor, "NEW_BFCO_IsPowerAttacking", 1);
            ActorUtils::SafeSetGraphVariableInt(a_actor, "NEW_BFCO_IsNormalAttacking", 0);
            return NotifyAnimation(a_actor, "attackStart");
        }

        // Vanilla power attack
        return NotifyAnimation(a_actor, "powerAttack");
    }

    bool ActionExecutor::ExecuteJump(RE::Actor *a_actor, const ActorStateData &a_state)
    {
        if (!a_actor) {
            return false;
        }

        // Check if already dodging/jumping - don't trigger another dodge
        bool isJumping = false;
        if (ActorUtils::SafeGetGraphVariableBool(a_actor, "EnhancedCombatAI_Jump", isJumping) && isJumping) {
            return false; // Already in jump state, don't trigger again
        }

        // Set EnhancedCombatAI_Jump to true BEFORE executing dodge
        // This allows OAR to detect it and replace the dodge animation with a jump animation
        ActorUtils::SafeSetGraphVariableBool(a_actor, "EnhancedCombatAI_Jump", true);

        // PHYSICS JUMP IMPULSE
        // Since we are hacking dodge to look like a jump, we need to provide the vertical movement manually.
        // SkyRim gravity is roughly -2000 units/sec^2.
        // A velocity of 600.0f gives a jump height of roughly h = v^2 / 2g ~= 360000 / 4000 = 90 units (approx player
        // jump height) We also add forward momentum to make it a "leap".

        // For now, let's keep it simple: Just UP. The Dodge animation has root motion for X/Y.
        // If we add X/Y velocity here, it might compound with root motion and launch them into orbit.
        // Let's rely on the anim for horizontal, and us for vertical.

        RE::NiPoint3 jumpVelocity(0.0f, 0.0f, 600.0f); // Vertical impulse only
        ActorUtils::SafeApplyVelocity(a_actor, jumpVelocity);

        return ExecuteDodge(a_actor, a_state);
    }

    void ActionExecutor::ResetJumpVariable(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return;
        }

        // Wrap in try-catch to protect against stale actors
        try {
            a_actor->SetGraphVariableBool("EnhancedCombatAI_Jump", false);
        } catch (...) {
            // Actor access failed - animation graph may be invalid
        }
    }

    void ActionExecutor::CalculateCPRCirclingAngles(const RE::NiPoint3 &a_strafeDirection,
                                                    const ActorStateData &a_state, float &a_outMinAngle,
                                                    float &a_outMaxAngle)
    {
        // Default angles (both sides) if we can't determine direction
        a_outMinAngle = 45.0f;
        a_outMaxAngle = 135.0f;

        if (!a_state.target.isValid) {
            return;
        }

        // Calculate direction from target to actor (target's perspective)
        RE::NiPoint3 toActor = a_state.self.position - a_state.target.position;
        toActor.z = 0.0f; // Keep on horizontal plane
        toActor.Unitize();

        // Get target's forward vector
        RE::NiPoint3 targetForward = a_state.target.forwardVector;
        targetForward.z = 0.0f;
        targetForward.Unitize();

        // Calculate perpendicular to target forward (right side)
        RE::NiPoint3 targetRight(-targetForward.y, targetForward.x, 0.0f);
        targetRight.Unitize();

        // Determine if strafe direction is to the right or left relative to target
        // Dot product with target's right vector: positive = right side, negative = left side
        float strafeDotRight = a_strafeDirection.Dot(targetRight);

        // Also check the direction from target to actor to determine which side we're on
        float actorDotRight = toActor.Dot(targetRight);

        // Use both strafe direction and actor position to determine circling side
        // If both indicate same side, use that side; otherwise use wider range
        bool strafeRight = strafeDotRight > 0.0f;
        bool actorRight = actorDotRight > 0.0f;

        if (strafeRight && actorRight) {
            // Both indicate right side - circle to the right (30-90 degrees)
            a_outMinAngle = 30.0f;
            a_outMaxAngle = 90.0f;
        } else if (!strafeRight && !actorRight) {
            // Both indicate left side - circle to the left (90-150 degrees)
            a_outMinAngle = 90.0f;
            a_outMaxAngle = 150.0f;
        } else {
            // Mixed signals - use wider range but favor the strafe direction
            if (strafeRight) {
                // Strafe right but actor on left - use right side but wider range
                a_outMinAngle = 30.0f;
                a_outMaxAngle = 105.0f;
            } else {
                // Strafe left but actor on right - use left side but wider range
                a_outMinAngle = 75.0f;
                a_outMaxAngle = 150.0f;
            }
        }
    }

    bool ActionExecutor::ExecuteBackoff(RE::Actor *a_actor, [[maybe_unused]] const DecisionResult &a_decision,
                                        [[maybe_unused]] const ActorStateData &a_state)
    {
        if (!a_actor) {
            return false;
        }

        // Try CPR backoff if available
        // NOTE: CPR backoff works for all actors (not just melee-only)
        if (IsCPRAvailable(a_actor)) {
            // Enable CPR backoff
            SetCPRBackoff(a_actor, 1.5f);
            return true;
        }

        // No CPR: let the native combat AI handle backoff instead of nudging with raw velocity.
        return false;
    }

    bool ActionExecutor::ExecuteFeint(RE::Actor *a_actor, const DecisionResult &a_decision,
                                      const ActorStateData &a_state)
    {
        if (!a_actor) {
            return false;
        }

        // Validate target before accessing target state
        if (!a_state.target.isValid) {
            return false; // Can't feint without a valid target
        }

        // Feinting strategy:
        // 1. Quick forward movement (appear aggressive)
        // 2. Then immediately strafe/dodge to bait enemy response

        // Check if we can attack (for the "fake" part)
        RE::ATTACK_STATE_ENUM attackState = ActorUtils::SafeGetAttackState(a_actor);
        if (attackState == RE::ATTACK_STATE_ENUM::kNone || attackState == RE::ATTACK_STATE_ENUM::kDraw) {
            // Start a quick bash as the "feint" (bash is faster than full attack)
            // This makes it look like we're committing to an attack
            bool bashSuccess = ExecuteBash(a_actor);

            // After bash, immediately prepare to strafe/dodge
            // The movement direction from decision should guide the follow-up
            if (bashSuccess) {
                // Set movement direction for follow-up strafe (baiting movement)
                // Use perpendicular direction to appear evasive
                RE::NiPoint3 toTarget = a_state.target.position - a_state.self.position;
                toTarget.z = 0.0f;
                toTarget.Unitize();

                // Perpendicular direction (strafe)
                RE::NiPoint3 feintDir(-toTarget.y, toTarget.x, 0.0f);
                feintDir.Unitize();

                // Apply movement with moderate intensity (not full commitment)
                SetMovementDirection(a_actor, feintDir, a_decision.intensity);
                return true;
            }
        }

        // Alternative: If bash failed or not available, use aggressive forward movement then retreat
        // This creates the "bait" effect
        RE::NiPoint3 forwardDir = a_decision.direction;
        if (forwardDir.x == 0.0f && forwardDir.y == 0.0f) {
            // Calculate forward direction if not provided
            forwardDir = a_state.target.position - a_state.self.position;
            forwardDir.z = 0.0f;
            forwardDir.Unitize();
        }

        // Quick forward movement (appear aggressive)
        SetMovementDirection(a_actor, forwardDir, a_decision.intensity * 0.7f);

        // Note: The actual "bait" follow-up (strafe/dodge) will happen in next frame
        // when the decision matrix evaluates again and sees the target's response
        return true;
    }

    void ActionExecutor::SetCPRAdvancing(RE::Actor *a_actor, float a_innerRadiusMin, float a_innerRadiusMid,
                                         float a_innerRadiusMax, float a_outerRadiusMin, float a_outerRadiusMid,
                                         float a_outerRadiusMax)
    {
        if (!a_actor) {
            return;
        }

        // IMPORTANT: Disable other CPR behaviors first to avoid conflicts
        ActorUtils::SafeSetGraphVariableBool(a_actor, "CPR_EnableCircling", false);
        ActorUtils::SafeSetGraphVariableBool(a_actor, "CPR_EnableBackoff", false);
        ActorUtils::SafeSetGraphVariableBool(a_actor, "CPR_EnableFallback", false);

        // Enable CPR advance radius override
        ActorUtils::SafeSetGraphVariableBool(a_actor, "CPR_EnableAdvanceRadius", true);
        ActorUtils::SafeSetGraphVariableFloat(a_actor, "CPR_InnerRadiusMin", a_innerRadiusMin);
        ActorUtils::SafeSetGraphVariableFloat(a_actor, "CPR_InnerRadiusMid", a_innerRadiusMid);
        ActorUtils::SafeSetGraphVariableFloat(a_actor, "CPR_InnerRadiusMax", a_innerRadiusMax);
        ActorUtils::SafeSetGraphVariableFloat(a_actor, "CPR_OuterRadiusMin", a_outerRadiusMin);
        ActorUtils::SafeSetGraphVariableFloat(a_actor, "CPR_OuterRadiusMid", a_outerRadiusMid);
        ActorUtils::SafeSetGraphVariableFloat(a_actor, "CPR_OuterRadiusMax", a_outerRadiusMax);

        // NOTE: CPR_InterruptAction removed temporarily to avoid potential crashes
        // CPR will handle advancing naturally through its behavior tree
        // If needed, we can re-enable this later with proper throttling
    }

    bool ActionExecutor::ExecuteAdvancing(RE::Actor *a_actor, const DecisionResult &a_decision,
                                          const ActorStateData &a_state)
    {
        if (!a_actor) {
            return false;
        }

        float currentDistance = a_state.target.isValid ? a_state.target.distance : 1000.0f;

        auto &config = Config::GetInstance();
        float desiredMinDist = config.GetDecisionMatrix().sprintAttackMinDistance;
        float desiredMaxDist = config.GetDecisionMatrix().sprintAttackMaxDistance;

        bool isMeleeOnly = IsMeleeOnlyActor(a_actor);
        bool cprAvailable = IsCPRAvailable(a_actor);

        // Sprint-charge: CPR (like ProjectGapClose) only shapes movement while the actor
        // is already advancing; it won't move a standing NPC. So actively put a melee
        // NPC into a sprint toward the target and give it a heading, then let vanilla +
        // CPR path/shape the charge. Reuses the sprint primitive from ExecuteSprintAttack.
        if (config.GetDecisionMatrix().enableSprintCharge && isMeleeOnly && a_state.target.isValid) {
            if (!ActorUtils::SafeIsSprinting(a_actor)) {
                NotifyAnimation(a_actor, "SprintStart");
                ActorUtils::SafeSetSprinting(a_actor, true);
            }
            SetMovementDirection(a_actor, a_decision.direction, a_decision.intensity);
        }

        if (cprAvailable && isMeleeOnly) {
            // Set inner radius (minimum engagement distance)
            float innerRadiusMin = desiredMinDist * 0.8f;
            float innerRadiusMid = desiredMinDist;
            float innerRadiusMax = desiredMinDist * 1.2f;

            // Set outer radius (maximum engagement distance - where we want to advance to)
            float outerRadiusMin = desiredMaxDist * 0.9f;
            float outerRadiusMid = desiredMaxDist;
            // Keep the outer radius just below the current distance at long range so
            // the actor stays "beyond outer radius", which per CPR = sprint toward the
            // target (fast gap-close). Inside the band would only walk at normal speed.
            float baseOuterRadiusMax = desiredMaxDist * 1.1f;
            float outerRadiusMax = (std::max)(baseOuterRadiusMax, currentDistance * 0.95f);

            // Enable CPR advancing
            SetCPRAdvancing(a_actor, innerRadiusMin, innerRadiusMid, innerRadiusMax, outerRadiusMin, outerRadiusMid,
                            outerRadiusMax);
            return true;
        }

        // No CPR available, or CPR only works for melee-only actors: let the native combat
        // AI handle advancing instead of nudging with raw velocity.
        return false;
    }
} // namespace CombatAI
