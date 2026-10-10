#pragma once

#include "ActorStateData.h"
#include "DecisionResult.h"
#include "DodgeSystem.h"
#include "pch.h"

namespace CombatAI
{
    // Executes actions (animations, movement)
    class ActionExecutor
    {
      public:
        ActionExecutor() = default;
        ~ActionExecutor() = default;

        // Execute a decision result
        bool Execute(RE::Actor *a_actor, const DecisionResult &a_decision, const ActorStateData &a_state);

        void EnableBFCO(bool isEnabled);

      private:
        // Execute bash action
        bool ExecuteBash(RE::Actor *a_actor, const ActorStateData *a_state = nullptr);

        // Execute parry (timed bash for parrying incoming attacks)
        bool ExecuteParry(RE::Actor *a_actor, const ActorStateData &a_state);

        // Execute timed block (timed block for blocking incoming attacks)
        bool ExecuteTimedBlock(RE::Actor *a_actor, const ActorStateData &a_state);

        // Execute dodge (TK Dodge integration)
        bool ExecuteDodge(RE::Actor *a_actor, const ActorStateData &a_state);

        // Execute a directional dodge toward the target for long-range advancing
        bool ExecuteForwardDodge(RE::Actor *a_actor, const DecisionResult &a_decision, const ActorStateData &a_state);

        void ConfigureDodgeSystem();

        // Execute strafe movement (with CPR support)
        bool ExecuteStrafe(RE::Actor *a_actor, const DecisionResult &a_decision, const ActorStateData &a_state);

        // Execute flanking movement (tactical positioning with allies, uses CPR)
        bool ExecuteFlanking(RE::Actor *a_actor, const DecisionResult &a_decision, const ActorStateData &a_state);

        // Execute retreat movement (with CPR support)
        bool ExecuteRetreat(RE::Actor *a_actor, const DecisionResult &a_decision, const ActorStateData &a_state);

        // Execute attack
        bool ExecuteAttack(RE::Actor *a_actor);

        // Execute power attack
        bool ExecutePowerAttack(RE::Actor *a_actor, const ActorStateData &a_state);

        // Execute sprint attack (BFCO support)
        bool ExecuteSprintAttack(RE::Actor *a_actor);

        // Execute jump (actually executes dodge with EnhancedCombatAI_Jump=true for OAR animation replacement)
        bool ExecuteJump(RE::Actor *a_actor, const ActorStateData &a_state);

        // Execute backoff movement (away from target when they're casting/drawing)
        bool ExecuteBackoff(RE::Actor *a_actor, const DecisionResult &a_decision, const ActorStateData &a_state);

        // Execute advancing movement (closing distance when too far)
        bool ExecuteAdvancing(RE::Actor *a_actor, const DecisionResult &a_decision, const ActorStateData &a_state);

        // Execute feint (fake attack to bait enemy response)
        bool ExecuteFeint(RE::Actor *a_actor, const DecisionResult &a_decision, const ActorStateData &a_state);

        // Helper: Trigger animation graph event
        bool NotifyAnimation(RE::Actor *a_actor, const char *a_eventName);

        // Helper: Set movement via animation graph (if possible)
        bool SetMovementDirection(RE::Actor *a_actor, const RE::NiPoint3 &a_direction, float a_intensity);

        // Helper: Check if CPR is available (check for graph variable support)
        bool IsCPRAvailable(RE::Actor *a_actor) const;

        // Helper: Check if actor is melee-only (no ranged weapons or magic)
        // CPR circling and advancing only work for melee-only actors
        bool IsMeleeOnlyActor(RE::Actor *a_actor) const;

        // Helper: Reset BFCO attack state
        void ResetBFCOAttackState(RE::Actor *a_actor);

        // Helper: Set CPR circling parameters
        void SetCPRCircling(RE::Actor *a_actor, float a_minDist, float a_maxDist, float a_minAngle, float a_maxAngle);

        // Helper: Set CPR fallback parameters
        void SetCPRFallback(RE::Actor *a_actor, float a_minDist, float a_maxDist, float a_minWait, float a_maxWait);

        // Helper: Set CPR backoff parameters
        void SetCPRBackoff(RE::Actor *a_actor, float a_minDistMult);

        // Helper: Set CPR advancing parameters
        void SetCPRAdvancing(RE::Actor *a_actor, float a_innerRadiusMin, float a_innerRadiusMid, float a_innerRadiusMax,
                             float a_outerRadiusMin, float a_outerRadiusMid, float a_outerRadiusMax);

        // Helper: Disable all CPR behaviors
        void DisableCPR(RE::Actor *a_actor);

        // Helper: Reset jump variable (when actor lands or when executing other actions)
        void ResetJumpVariable(RE::Actor *a_actor);

        // Helper: Calculate CPR circling angles based on strafe direction
        // CPR angles are relative to target's facing direction (0° = front, 90° = side, 180° = back)
        void CalculateCPRCirclingAngles(const RE::NiPoint3 &a_strafeDirection, const ActorStateData &a_state,
                                        float &a_outMinAngle, float &a_outMaxAngle);

        // Dodge system
        DodgeSystem m_dodgeSystem;
        bool m_isBFCOEnabled = false;
    };
} // namespace CombatAI
