#include "ActionExecutor.h"
#include "BlockRules.h"
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


        // Moves run through our movement tree nodes, which don't need the block dropped first: stopping it only lets
        // vanilla combat AI start blocking again at once. A forward dodge does need it released.
        bool movementCanRun = true;
        switch (a_decision.action) {
        case ActionType::Strafe:
        case ActionType::Flanking:
        case ActionType::Retreat:
        case ActionType::Backoff:
        case ActionType::Advancing:
            movementCanRun = false;
            break;
        case ActionType::ForwardDodge:
            movementCanRun = a_state.target.isValid;
            break;
        default:
            break;
        }

        bool blockReleased = false;
        // A held direct block (DualBlock node) stays up while the NPC moves (run 35: a Feint cut one 0.15 s early)
        if (a_state.self.isBlocking && movementCanRun && !Intents::WantsDualBlock(a_actor)) {
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
            // A dodge that can't go fails here; CombatDirector then re-runs it as Advancing (and records Advancing)
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

    bool ActionExecutor::ExecuteBash(RE::Actor *a_actor, [[maybe_unused]] const ActorStateData *a_state)
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

        // With Simple Timed Block loaded, its window spell (0.33 s) makes this a timed block and must go on
        // before blocking starts. Without the mod it is a well-timed plain block.
        auto &timedBlock = TimedBlockIntegration::GetInstance();
        if (timedBlock.IsAvailable() && !timedBlock.ApplyTimedBlockWindow(a_actor)) {
            LOG_WARN("Failed to apply timed block window spell - blocking without it");
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

        // Without the Block tree (a weapon with a spell, torch or second weapon): our DualBlock node holds a direct
        // block until the swing is over (CombatDirector ends the hold once the hit lands or the swing stops), at most
        // 1.2 s. Run 39: holding to the predicted hit + 0.25 s ran out before 7 of 17 blows, which landed 0.56-0.78 s
        // after the block went up (the player's swings came in two speeds, 0.12-0.50 s and 0.63-1.11 s).
        if (!BlockRules::CanRunBlockTree(a_actor)) {
            constexpr float kDualBlockMaxHold = 1.2f;
            Intents::PostDualBlock(a_actor, kDualBlockMaxHold);
            return true;
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

        // Our CloseMovement tree runs the vanilla Circle node on the brain's side
        Intents::PostMove(a_actor, Intents::Move::Circle, CircleSide(a_state, movementDir));


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

        // Flanking circles like Strafe (vanilla Circle node, brain's side)
        Intents::PostMove(a_actor, Intents::Move::Circle, CircleSide(a_state, flankDir));


        return true; // the Circle node moves it
    }

    bool ActionExecutor::ExecuteRetreat(RE::Actor *a_actor, [[maybe_unused]] const DecisionResult &a_decision,
                                        [[maybe_unused]] const ActorStateData &a_state)
    {
        if (!a_actor) {
            return false;
        }

        // Our CloseMovement tree runs the vanilla Fallback node
        Intents::PostMove(a_actor, Intents::Move::Fallback);

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
        // Only from a real sprint (the caller advances until then). A plain
        // "attackStart" here would play a standing swing from sprint distance.
        if (!CanLaunchSprintAttack(a_actor, a_state)) {
            return false;
        }

        return NotifyAnimation(a_actor, "attackStartSprint");
    }

    void ActionExecutor::SuspendRetreat(RE::Actor *a_actor)
    {
        if (Intents::WantsMove(a_actor, Intents::Move::Fallback) || Intents::WantsMove(a_actor, Intents::Move::Backoff)) {
            Intents::ClearMove(a_actor);
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

    bool ActionExecutor::ExecuteBackoff(RE::Actor *a_actor, [[maybe_unused]] const DecisionResult &a_decision,
                                        [[maybe_unused]] const ActorStateData &a_state)
    {
        if (!a_actor) {
            return false;
        }

        // Our CloseMovement tree runs the vanilla Backoff node
        Intents::PostMove(a_actor, Intents::Move::Backoff);

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

    bool ActionExecutor::ExecuteAdvancing(RE::Actor *a_actor, const DecisionResult &a_decision,
                                          const ActorStateData &a_state)
    {
        if (!a_actor) {
            return false;
        }

        const auto &matrix = Config::GetInstance().GetDecisionMatrix();

        // Our CloseMovement tree runs the vanilla Advance node. It stops
        // a little inside the brain's attack range, not at the engine's longer melee range (run 16: a reach-82
        // bandit parked at 125-175 units, outside its own range, and the brain never attacked).
        // A ranged fighter's advance gives its own stop distance (EvaluateRangedPositioning)
        const float stopDistance =
            a_decision.holdDistance > 0.0f
                ? a_decision.holdDistance
                : 0.9f * StateHelpers::EffectiveAttackRange(a_state.weaponReach, a_state.target.boundRadius,
                                                            matrix.offenseReachMultiplier, matrix.offenseReachSafetyMargin);
        Intents::PostMove(a_actor, Intents::Move::Advance, stopDistance);

        // The tree's Advance node moves the NPC (vanilla pathing and speed). The old forced sprint "charge" (raw sprint
        // flag + heading) was removed: NPCs sometimes ran in place with it.

        return true; // the Advance node moves it
    }
} // namespace CombatAI
