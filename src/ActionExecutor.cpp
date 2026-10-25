#include "ActionExecutor.h"
#include "Intents.h"
#include "ActorUtils.h"
#include "Config.h"
#include "EnvSensor.h"
#include "GuardCounterFeedbackTracker.h"
#include "ParryFeedbackTracker.h"
#include "TimedBlockFeedbackTracker.h"
#include "TimedBlockIntegration.h"
#include "pch.h"

#include <random>
#include <xmmintrin.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace CombatAI
{
    namespace
    {
        // FluidMotionNPC (optional): while it moves an actor (jump arc, route, climb) we don't start a jump,
        // and during ours we claim the actor so it starts nothing. Resolved by export name; absent = no-op.
        namespace FluidMotionNPC
        {
            using IsTraversingFn = bool (*)(std::uint32_t);
            using ClaimFn = void (*)(std::uint32_t, float);

            HMODULE Module()
            {
                static const HMODULE module = GetModuleHandleA("FluidMotionNPC.dll");
                return module;
            }

            bool IsTraversing(RE::FormID a_formID)
            {
                static const auto fn = Module() ? reinterpret_cast<IsTraversingFn>(GetProcAddress(Module(), "FMNPC_IsTraversing")) : nullptr;
                return fn && fn(a_formID);
            }

            void Claim(RE::FormID a_formID, float a_seconds)
            {
                static const auto fn = Module() ? reinterpret_cast<ClaimFn>(GetProcAddress(Module(), "FMNPC_Claim")) : nullptr;
                if (fn) {
                    fn(a_formID, a_seconds);
                }
            }
        } // namespace FluidMotionNPC
    } // namespace

    // Circle side of a strafe direction: +1 when it turns left (counter-clockwise, z up) from the line
    // to the target, -1 right, 0 when there is no direction.
    float ActionExecutor::CircleSide(const ActorStateData &a_state, const RE::NiPoint3 &a_direction)
    {
        const RE::NiPoint3 toTarget = a_state.target.position - a_state.self.position;
        const float cross = toTarget.x * a_direction.y - toTarget.y * a_direction.x;
        return cross > 0.0f ? 1.0f : (cross < 0.0f ? -1.0f : 0.0f);
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
            ReleaseSprint(a_actor);
            if (ActorUtils::SafeIsSprinting(a_actor)) {
                ActorUtils::SafeSetSprinting(a_actor, false);
            }
            break;
        case ActionType::Advancing:
        case ActionType::SprintAttack:
            break;
        default:
            // A forced sprint left on while backing off/strafing plays the forward sprint
            // animation while the controller moves the actor elsewhere (running backward)
            ReleaseSprint(a_actor);
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
            success = ExecuteSprintAttack(a_actor, a_state);
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
            // A direction means a sidestep in that direction (projectile dodge); without one,
            // evasion picks its side from the melee attacker
            success = (a_decision.direction.x != 0.0f || a_decision.direction.y != 0.0f)
                          ? ExecuteDirectionalDodge(a_actor, a_decision.direction)
                          : ExecuteDodge(a_actor, a_state);
            break;

        case ActionType::ForwardDodge:
            // A dodge that can't go (path, stamina, already dodging) still closes in: run 30 stood still on 17 ticks
            success = ExecuteForwardDodge(a_actor, a_decision, a_state) || ExecuteAdvancing(a_actor, a_decision, a_state);
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

        // Our Block tree runs the vanilla Bash node (real action, attack data, arbitration)
        return Intents::PostDefend(a_actor, Intents::Defend::Bash);
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

        // Always record parry attempt - this is specifically a parry action
        if (a_state.target.isValid && (a_state.target.isAttacking || a_state.target.isPowerAttacking) &&
            a_state.temporal.target.timeUntilAttackHits < 999.0f) {
            // Get target actor
            RE::Actor *target = nullptr;
            RE::CombatController *combatController = ActorUtils::SafeGetCombatController(a_actor);
                if (combatController) {
                RE::ActorHandle targetHandle = combatController->targetHandle;
                RE::NiPointer<RE::Actor> targetPtr = targetHandle.get();
                if (targetPtr && targetPtr.get()) {
                    target = targetPtr.get();
                }
            }

            if (target) {
                ParryFeedbackTracker::GetInstance().RecordParryAttempt(a_actor, target,
                                                                       a_state.temporal.target.estimatedAttackDuration,
                                                                       a_state.temporal.target.timeUntilAttackHits);
            }
        }

        // Parry = a timed bash. The decision matrix already timed it; our Block tree runs the
        // vanilla Bash node as soon as it re-reads (every 0.05 s).
        return Intents::PostDefend(a_actor, Intents::Defend::Bash);
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
            RE::CombatController *combatController = ActorUtils::SafeGetCombatController(a_actor);
                if (combatController) {
                RE::ActorHandle targetHandle = combatController->targetHandle;
                RE::NiPointer<RE::Actor> targetPtr = targetHandle.get();
                if (targetPtr && targetPtr.get()) {
                    target = targetPtr.get();
                }
            }

            if (target) {
                TimedBlockFeedbackTracker::GetInstance().RecordTimedBlockAttempt(
                    a_actor, target, a_state.temporal.target.estimatedAttackDuration,
                    a_state.temporal.target.timeUntilAttackHits);
            }
        }

        // Our Block tree runs the vanilla Block node; the timed-block window above is already applied
        return Intents::PostDefend(a_actor, Intents::Defend::Block);
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
        return m_dodgeSystem.ExecuteDodge(a_actor, {direction},
                                          RE::TESForm::LookupByID<RE::TESObjectREFR>(a_state.target.targetFormID));
    }

    bool ActionExecutor::ExecuteDirectionalDodge(RE::Actor *a_actor, RE::NiPoint3 a_direction)
    {
        if (!a_actor) {
            return false;
        }
        // Either side clears a projectile; the mirror side if this one is blocked or a ledge
        ConfigureDodgeSystem();
        return m_dodgeSystem.ExecuteDodge(a_actor, {a_direction, -a_direction});
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

        // Our CloseMovement tree runs the vanilla Circle node on the brain's side; CPR (below) only
        // shapes it if present
        Intents::PostMove(a_actor, Intents::Move::Circle, CircleSide(a_state, movementDir));

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

        // No direct velocity nudge (it fights the engine and moves unpathed): the Circle node moves it
        return true;
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

        // Flanking circles like Strafe (vanilla Circle node, brain's side); CPR below shapes the ring
        Intents::PostMove(a_actor, Intents::Move::Circle, CircleSide(a_state, flankDir));

        // Try CPR circling if available and actor is melee-only
        bool isMeleeOnly = IsMeleeOnlyActor(a_actor);
        if (IsCPRAvailable(a_actor) && isMeleeOnly) {
            // Calculate circling parameters for flanking movement
            float minDist = (std::max)(50.0f, a_state.target.distance * 0.7f);
            float maxDist = a_state.target.distance * 1.3f;
            if (a_decision.holdDistance > 0.0f) {
                // Pacing: wait on a ring outside reach
                minDist = a_decision.holdDistance;
                maxDist = (std::max)(maxDist, a_decision.holdDistance + 150.0f);
            }

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

        return true; // the Circle node moves it
    }

    bool ActionExecutor::ExecuteRetreat(RE::Actor *a_actor, [[maybe_unused]] const DecisionResult &a_decision,
                                        const ActorStateData &a_state)
    {
        if (!a_actor) {
            return false;
        }

        // Our CloseMovement tree runs the vanilla Fallback node; CPR (below) only shapes it if present
        Intents::PostMove(a_actor, Intents::Move::Fallback);

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

        return true; // the Fallback node moves it
    }

    bool ActionExecutor::CanLaunchSprintAttack(RE::Actor *a_actor, const ActorStateData &a_state)
    {
        if (!a_actor || !a_state.target.isValid) {
            return false;
        }
        const RE::FormID formID = a_actor->GetFormID();
        if (!ActorUtils::SafeIsSprinting(a_actor)) {
            m_sprintSince.Erase(formID);
            return false;
        }
        // The sprint flag flips before the behavior graph reaches its sprint state, and
        // "attackStartSprint" is ignored until it does.
        constexpr auto kSustainedSprint = std::chrono::milliseconds(300);
        const auto now = std::chrono::steady_clock::now();
        // The skill roll (Humanizer) decides whether the NPC goes for it; a second
        // Confidence roll here stacked to ~25% per approach and nothing ever launched.
        m_sprintSince.Emplace(formID, now); // keeps the first time seen
        if (now - m_sprintSince.Find(formID).value_or(now) < kSustainedSprint) {
            return false;
        }
        if (Config::GetInstance().GetEnvSensor().enableLosGate && a_state.env.losChecked &&
            !a_state.env.losToTarget) {
            return false;
        }
        const auto attackState = ActorUtils::SafeGetAttackState(a_actor);
        if (attackState != RE::ATTACK_STATE_ENUM::kNone && attackState != RE::ATTACK_STATE_ENUM::kDraw) {
            return false;
        }
        return a_state.target.distance <= SprintAttackLaunchDistance(a_state);
    }

    float ActionExecutor::SprintAttackLaunchDistance(const ActorStateData &a_state)
    {
        const float configured = Config::GetInstance().GetDecisionMatrix().sprintAttackLaunchDistance;
        return configured > 0.0f ? configured : 3.0f * a_state.weaponReach;
    }

    bool ActionExecutor::ExecuteSprintAttack(RE::Actor *a_actor, const ActorStateData &a_state)
    {
        // The caller runs up first; this only fires from a real sprint. A plain
        // "attackStart" here would play a standing swing from sprint distance.
        if (!CanLaunchSprintAttack(a_actor, a_state)) {
            return false;
        }

        return NotifyAnimation(a_actor, "attackStartSprint");
    }

    void ActionExecutor::ForceSprint(RE::Actor *a_actor)
    {
        if (!ActorUtils::SafeIsSprinting(a_actor)) {
            NotifyAnimation(a_actor, "SprintStart");
            ActorUtils::SafeSetSprinting(a_actor, true);
        }
        m_forcedSprint.Insert(a_actor->GetFormID());
    }

    void ActionExecutor::SuspendRetreat(RE::Actor *a_actor)
    {
        if (Intents::WantsMove(a_actor, Intents::Move::Fallback) || Intents::WantsMove(a_actor, Intents::Move::Backoff)) {
            Intents::ClearMove(a_actor);
        }
        for (const char *var : {"CPR_EnableFallback", "CPR_EnableBackoff"}) {
            bool enabled = false;
            if (ActorUtils::SafeGetGraphVariableBool(a_actor, var, enabled) && enabled) {
                ActorUtils::SafeSetGraphVariableBool(a_actor, var, false);
            }
        }
    }

    void ActionExecutor::ReleaseSprint(RE::Actor *a_actor)
    {
        if (!a_actor || m_forcedSprint.Erase(a_actor->GetFormID()) == 0) {
            return;
        }
        if (ActorUtils::SafeIsSprinting(a_actor)) {
            ActorUtils::SafeSetSprinting(a_actor, false);
            NotifyAnimation(a_actor, "SprintStop");
        }
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
        RE::NiPoint3 flatDir(worldDir.x, worldDir.y, 0.0f);
        flatDir.Unitize();

        // Cliff-safe movement: strafe/flank/feint push the controller directly, so never
        // walk the actor off a ledge (same probe as cliff-safe dodge).
        const auto &envCfg = Config::GetInstance().GetEnvSensor();
        if (envCfg.enableCliffSafeDodge &&
            !EnvSensor::HasGroundAhead(a_actor, flatDir, envCfg.dodgeLedgeProbeDistance, envCfg.dodgeMaxDrop)) {
            return false;
        }

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
        // Apply horizontal velocity (overrides vanilla pathfinding velocity for this frame)
        // Physics Speed needs to match Animation Speed to look natural (game units/s)
        float physicsSpeed = 300.0f * speedNorm;
        ActorUtils::SafeApplyHorizontalVelocity(a_actor, flatDir * physicsSpeed);

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
        auto rightHand = ActorUtils::SafeGetEquippedObject(a_actor, false);

        // Check right hand for ranged weapon
        if (rightHand) {
            if (rightHand->IsWeapon()) {
                auto weapon = rightHand->As<RE::TESObjectWEAP>();
                if (weapon) {
                    if (weapon->IsBow() || weapon->IsCrossbow()) {
                        return false; // Has ranged weapon
                    }
                }
            }
        }

        // Check if actor is currently casting magic
        // This is more reliable than checking left hand (which could be shield)
        if (ActorUtils::SafeWhoIsCasting(a_actor) != 0) {
            return false; // Actor is casting magic
        }

        return true; // Melee-only actor
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

        // The vanilla event; MCO, BFCO and SCAR movesets all take over from it
        return Intents::PostAttack(a_actor, Intents::Attack::Light); // run by our Melee tree
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
            RE::CombatController *combatController = ActorUtils::SafeGetCombatController(a_actor);
                if (combatController) {
                RE::ActorHandle targetHandle = combatController->targetHandle;
                RE::NiPointer<RE::Actor> targetPtr = targetHandle.get();
                if (targetPtr && targetPtr.get()) {
                    target = targetPtr.get();
                }
            }

            if (target) {
                GuardCounterFeedbackTracker::GetInstance().RecordGuardCounterAttempt(a_actor, target);
            }
        }

        // Our Melee tree runs the vanilla Attack node, restricted to power attack data
        return Intents::PostAttack(a_actor, Intents::Attack::Power);
    }

    bool ActionExecutor::ExecuteJump(RE::Actor *a_actor, const ActorStateData &a_state)
    {
        if (!a_actor) {
            return false;
        }

        const RE::FormID formID = a_actor->GetFormID();
        if (m_activeJumps.Contains(formID) || FluidMotionNPC::IsTraversing(formID)) {
            return false; // Still airborne, or FluidMotionNPC is moving this actor
        }

        // Check everything up front so a rejected jump leaves no graph/physics side effects
        const auto *controller = a_actor->GetCharController();
        if (a_actor->IsInKillMove() || ActorUtils::SafeGetKnockState(a_actor) != RE::KNOCK_STATE_ENUM::kNormal ||
            !controller || controller->context.currentState != RE::hkpCharacterStateType::kOnGround) {
            return false;
        }
        // No cooldown: each jump costs stamina, so the next one waits until the NPC has it back
        const float staminaCost = Config::GetInstance().GetDecisionMatrix().jumpStaminaCost;
        auto *actorValues = ActorUtils::SafeAsActorValueOwner(a_actor);
        if (!actorValues || actorValues->GetActorValue(RE::ActorValue::kStamina) < staminaCost) {
            return false;
        }

        // Plan before any event: a sideways leap out of the shooter's line (random side first), then the
        // other side, then straight up. Each arc is ray-checked for walls, ceiling and a place to land.
        const float height = Config::GetInstance().GetDecisionMatrix().jumpHeight;
        RE::NiPoint3 toTarget = a_state.target.position - a_state.self.position;
        toTarget.z = 0.0f;
        const RE::NiPoint3 left = toTarget.Length() > 1.0f ? RE::NiPoint3(-toTarget.y, toTarget.x, 0.0f) / toTarget.Length()
                                                            : RE::NiPoint3(-a_state.self.forwardVector.y, a_state.self.forwardVector.x, 0.0f);
        thread_local std::mt19937 gen{std::random_device{}()};
        const float first = std::bernoulli_distribution(0.5)(gen) ? 1.0f : -1.0f;
        ActiveJump jump;
        const char *shape = nullptr;
        if (PlanJumpArc(a_actor, left * first, kJumpSideDistance, height, jump)) {
            shape = "side";
        } else if (PlanJumpArc(a_actor, left * -first, kJumpSideDistance, height, jump)) {
            shape = "other side";
        } else if (PlanJumpArc(a_actor, RE::NiPoint3(), 0.0f, height, jump)) {
            shape = "in place";
        } else {
            if (Config::GetInstance().IsDebugTarget(formID)) {
                LOG_DEBUG("Jump 0x{:08X}: no clear arc (walls, ceiling or no landing) - not jumping", formID);
            }
            return false;
        }

        // FluidMotionNPC's NPC start event, then the standing jump; a graph that takes neither gets no jump
        // (no animation would mean a frozen pose sliding along the arc)
        if (!ActorUtils::SafeNotifyAnimationGraph(a_actor, "JumpDirectionalStart") &&
            !ActorUtils::SafeNotifyAnimationGraph(a_actor, REL::Module::IsSE() ? "JumpUp" : "JumpStandingStart")) {
            if (Config::GetInstance().IsDebugTarget(formID)) {
                LOG_DEBUG("Jump 0x{:08X}: graph took no jump start event - not jumping", formID);
            }
            return false;
        }

        actorValues->DamageActorValue(RE::ActorValue::kStamina, staminaCost);
        jump.handle = a_actor->GetHandle();
        FluidMotionNPC::Claim(formID, jump.T + kJumpSettleSeconds + 0.5f);
        m_activeJumps.WithWriteLock([&](auto &jumps) { jumps.insert_or_assign(formID, jump); });
        if (Config::GetInstance().IsDebugTarget(formID)) {
            LOG_DEBUG("Jump 0x{:08X}: {} arc, {:.0f} units, landing dz {:+.0f}, {:.2f}s", formID, shape,
                      std::hypot(jump.end.x - jump.start.x, jump.end.y - jump.start.y), jump.end.z - jump.start.z,
                      jump.T);
        }
        return true;
    }

    // Ballistic arc from the actor's feet, a_distance along a_direction (unit, z 0), apex a_height above the
    // start. False when a wall or ceiling is in the way, or there is no ground to land on. Actor hits are
    // ignored (bodies are not walls; the kLOS ray hits them).
    bool ActionExecutor::PlanJumpArc(RE::Actor *a_actor, const RE::NiPoint3 &a_direction, float a_distance,
                                     float a_height, ActiveJump &a_out)
    {
        const auto pos = ActorUtils::SafeGetPosition(a_actor);
        if (!pos) {
            return false;
        }
        const RE::NiPoint3 start = *pos;
        auto blocked = [&](const RE::NiPoint3 &a_from, const RE::NiPoint3 &a_to) {
            const auto hit = EnvSensor::CastRay(a_actor, a_from, a_to);
            return hit.hit && !(hit.ref && hit.ref->As<RE::Actor>());
        };

        // Landing: first non-actor surface straight down through the landing spot (in place: where it
        // stands; that ray would only find the jumper's own body).
        RE::NiPoint3 end = start + a_direction * a_distance;
        if (a_distance > 0.0f) {
            const float maxDrop = Config::GetInstance().GetEnvSensor().dodgeMaxDrop;
            const RE::NiPoint3 probeFrom{end.x, end.y, start.z + a_height + 40.0f};
            const RE::NiPoint3 probeTo{end.x, end.y, start.z - maxDrop};
            const auto ground = EnvSensor::CastRay(a_actor, probeFrom, probeTo);
            if (!ground.hit || (ground.ref && ground.ref->As<RE::Actor>())) {
                return false; // ledge (or landing on someone)
            }
            end.z = probeFrom.z + (probeTo.z - probeFrom.z) * ground.fraction;
        }
        const float dz = end.z - start.z;
        if (dz > a_height * 0.5f) {
            return false; // too high a step to land on
        }

        // z(t) = start.z + vz t - g t^2 / 2, apex a_height; T is when it comes down to end.z.
        const float vz = std::sqrt(2.0f * kJumpGravity * a_height);
        const float T = (vz + std::sqrt(vz * vz - 2.0f * kJumpGravity * dz)) / kJumpGravity;
        if (!std::isfinite(T) || T <= 0.0f) {
            return false;
        }
        a_out.start = start;
        a_out.end = end;
        a_out.vel = {(end.x - start.x) / T, (end.y - start.y) / T, vz};
        a_out.T = T;

        // Sweep the arc at shin, waist and head height (humanoid; head ray also covers the ceiling).
        constexpr int kSegments = 8;
        constexpr float kHeights[] = {18.0f, 60.0f, 112.0f};
        RE::NiPoint3 prev = start;
        for (int s = 1; s <= kSegments; ++s) {
            const RE::NiPoint3 cur = s == kSegments ? end : a_out.At(T * s / kSegments);
            for (const float h : kHeights) {
                if (blocked(prev + RE::NiPoint3(0.0f, 0.0f, h), cur + RE::NiPoint3(0.0f, 0.0f, h))) {
                    return false;
                }
            }
            prev = cur;
        }
        return true;
    }

    // Runs once per frame on the main thread. While airborne the actor is placed on its arc each frame
    // (position, matching velocity, in-air state, fall height reset so landing is no fall); after touchdown
    // a short settle stops a bounce, and one JumpLand retry frees a graph left in its jump state.
    void ActionExecutor::UpdateJumps(float a_deltaTime)
    {
        if (m_activeJumps.Empty()) {
            return;
        }

        m_activeJumps.WithWriteLock([&](auto &jumps) {
            for (auto it = jumps.begin(); it != jumps.end();) {
                ActiveJump &jump = it->second;
                const auto ptr = jump.handle.get();
                RE::Actor *actor = ptr.get();
                const bool debug = Config::GetInstance().IsDebugTarget(it->first);
                if (!actor || actor->IsDead() || !actor->Is3DLoaded() || actor->IsInRagdollState() ||
                    ActorUtils::SafeGetKnockState(actor) != RE::KNOCK_STATE_ENUM::kNormal) {

                    if (debug) {
                        LOG_DEBUG("Jump 0x{:08X}: aborted at {:.2f}/{:.2f}s", it->first, jump.t, jump.T);
                    }
                    it = jumps.erase(it);
                    continue;
                }

                auto *controller = actor->GetCharController();
                const float scale = RE::bhkWorld::GetWorldScale();
                if (jump.settle < 0.0f) {
                    jump.t = std::min(jump.t + a_deltaTime, jump.T);
                    const bool airborne = jump.t < jump.T;
                    const RE::NiPoint3 p = airborne ? jump.At(jump.t) : jump.end;
                    const RE::NiPoint3 v = airborne ? RE::NiPoint3(jump.vel.x, jump.vel.y, jump.vel.z - kJumpGravity * jump.t)
                                                    : RE::NiPoint3(jump.vel.x * 0.3f, jump.vel.y * 0.3f, 0.0f);
                    actor->SetPosition(p, true);
                    if (controller) {
                        controller->context.currentState =
                            airborne ? RE::hkpCharacterStateType::kInAir : RE::hkpCharacterStateType::kOnGround;
                        controller->SetLinearVelocityImpl(RE::hkVector4(v.x * scale, v.y * scale, v.z * scale, 0.0f));
                        controller->fallStartHeight = p.z;
                        controller->fallTime = 0.0f;
                    }
                    if (!airborne) {
                        ActorUtils::SafeNotifyAnimationGraph(actor, "JumpLand");
                        jump.settle = 0.0f;
                        if (debug) {
                            LOG_DEBUG("Jump 0x{:08X}: landed after {:.2f}s", it->first, jump.T);
                        }
                    }
                    ++it;
                    continue;
                }

                jump.settle += a_deltaTime;
                RE::NiPoint3 v;
                actor->GetLinearVelocity(v);
                if (jump.settle < kJumpSettleSeconds && controller) {
                    if (v.z > 0.0f) {
                        controller->SetLinearVelocityImpl(RE::hkVector4(v.x * scale, v.y * scale, 0.0f, 0.0f));
                    }
                    controller->fallStartHeight = actor->GetPositionZ();
                    controller->fallTime = 0.0f;
                }
                // Graph ignored JumpLand and skates on in its jump state: one retry, late enough that a normal
                // landing animation is over (an early resend replays it).
                if (!jump.relanded && jump.settle >= kJumpRelandSeconds && actor->IsInJumpState() &&
                    std::hypot(v.x, v.y) > 60.0f) {
                    jump.relanded = true;
                    const bool accepted = ActorUtils::SafeNotifyAnimationGraph(actor, "JumpLand");
                    if (debug) {
                        LOG_DEBUG("Jump 0x{:08X}: still in jump state after landing - JumpLand resent ({})", it->first,
                                  accepted);
                    }
                }
                if (jump.settle > kJumpRelandSeconds + 0.5f) {
                    it = jumps.erase(it);
                } else {
                    ++it;
                }
            }
        });
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

        // Our CloseMovement tree runs the vanilla Backoff node; CPR (below) only shapes it if present
        Intents::PostMove(a_actor, Intents::Move::Backoff);

        // Try CPR backoff if available
        // NOTE: CPR backoff works for all actors (not just melee-only)
        if (IsCPRAvailable(a_actor)) {
            // Enable CPR backoff
            SetCPRBackoff(a_actor, 1.5f);
            return true;
        }

        return true; // the Backoff node moves it
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

        auto &config = Config::GetInstance();
        const auto &matrix = config.GetDecisionMatrix();
        float desiredMaxDist = matrix.sprintAttackMaxDistance;

        bool isMeleeOnly = IsMeleeOnlyActor(a_actor);
        bool cprAvailable = IsCPRAvailable(a_actor);

        // Our CloseMovement tree runs the vanilla Advance node (CPR below only shapes it if present). It stops
        // a little inside the brain's attack range, not at the engine's longer melee range (run 16: a reach-82
        // bandit parked at 125-175 units, outside its own range, and the brain never attacked).
        const float stopDistance = 0.9f * StateHelpers::EffectiveAttackRange(a_state.weaponReach, a_state.target.boundRadius,
                                                                             matrix.offenseReachMultiplier,
                                                                             matrix.offenseReachSafetyMargin);
        Intents::PostMove(a_actor, Intents::Move::Advance, stopDistance);

        // Sprint-charge: CPR (like ProjectGapClose) only shapes movement while the actor
        // is already advancing; it won't move a standing NPC. So actively put a melee
        // NPC into a sprint toward the target and give it a heading, then let vanilla +
        // CPR path/shape the charge. Reuses the sprint primitive from ExecuteSprintAttack.
        if (config.GetDecisionMatrix().enableSprintCharge && isMeleeOnly && a_state.target.isValid) {
            ForceSprint(a_actor);
            SetMovementDirection(a_actor, a_decision.direction, a_decision.intensity);
        } else {
            ReleaseSprint(a_actor);
        }

        if (cprAvailable && isMeleeOnly) {
            // Inner radius = where CPR stops advancing. It must be melee range: taking it
            // from the sprint-attack band parked NPCs just outside their reach.
            const float meleeRange = StateHelpers::EffectiveAttackRange(
                a_state.weaponReach, a_state.target.boundRadius, matrix.offenseReachMultiplier,
                matrix.offenseReachSafetyMargin);
            float innerRadiusMin = meleeRange * 0.6f;
            float innerRadiusMid = meleeRange * 0.8f;
            float innerRadiusMax = meleeRange;

            // Outer radius: beyond it CPR sprints. An offensive NPC uses OuterMin..OuterMid,
            // so keep that at the sprint-attack launch distance (as ProjectGapClose does):
            // the NPC is still sprinting when it reaches launch range. Taking it from the
            // sprint band slowed NPCs to a run ~150 units before they could launch.
            // CPR requires each outer value to exceed the matching inner one.
            // Offensive NPCs stop sprinting at 60-85% of the launch distance, so they cross
            // the launch line still sprinting (0.8-1.0 left a window one tick wide).
            const float launchDist = SprintAttackLaunchDistance(a_state);
            float outerRadiusMin = (std::max)(launchDist * 0.6f, innerRadiusMax + 20.0f);
            float outerRadiusMid = (std::max)(launchDist * 0.85f, outerRadiusMin + 1.0f);
            float outerRadiusMax = (std::max)(desiredMaxDist * 1.1f, outerRadiusMid + 1.0f);

            // Enable CPR advancing
            SetCPRAdvancing(a_actor, innerRadiusMin, innerRadiusMid, innerRadiusMax, outerRadiusMin, outerRadiusMid,
                            outerRadiusMax);
            return true;
        }

        return true; // the Advance node moves it
    }
} // namespace CombatAI
