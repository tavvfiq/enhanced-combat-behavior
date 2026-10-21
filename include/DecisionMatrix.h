#pragma once

#include "ActorStateData.h"
#include "CombatStyleEnhancer.h"
#include "DecisionResult.h"

namespace CombatAI
{
    // Evaluates state data and makes tactical decisions
    class DecisionMatrix
    {
      public:
        DecisionMatrix() = default;
        ~DecisionMatrix() = default;

        // Evaluate state and return decision
        DecisionResult Evaluate(RE::Actor *a_actor, const ActorStateData &a_state);

        // Held-back action for combat pacing: when an actor is denied an attack slot it
        // should stay engaged (circle/flank the target) instead of committing. Returns a
        // ready Flanking decision (execution falls back to strafe if CPR is unavailable).
        DecisionResult EvaluateHeldBack(RE::Actor *a_actor, const ActorStateData &a_state);

      private:
        // Interrupt (Counter-Play)
        DecisionResult EvaluateInterrupt(RE::Actor *a_actor, const ActorStateData &a_state);

        // Evasion (Tactical)
        DecisionResult EvaluateEvasion(RE::Actor *a_actor, const ActorStateData &a_state);

        // Sidestep a projectile on course to hit (engine threat list)
        DecisionResult EvaluateProjectileDodge(RE::Actor *a_actor, const ActorStateData &a_state);

        // Survival (Fallback)
        DecisionResult EvaluateSurvival(RE::Actor *a_actor, const ActorStateData &a_state);

        // Offense
        DecisionResult EvaluateOffense(RE::Actor *a_actor, const ActorStateData &a_state);

        // Backoff (when target is casting/drawing bow)
        DecisionResult EvaluateBackoff(RE::Actor *a_actor, const ActorStateData &a_state);

        // Flanking & Positioning (tactical positioning with allies)
        DecisionResult EvaluateFlanking(RE::Actor *a_actor, const ActorStateData &a_state);

        // Feinting & Baiting (fake attacks to bait enemy response)
        DecisionResult EvaluateFeinting(RE::Actor *a_actor, const ActorStateData &a_state);

        // Parry (timed bash for parrying incoming attacks)
        DecisionResult EvaluateParry(RE::Actor *a_actor, const ActorStateData &a_state);

        // Timed Block (timed block for blocking incoming attacks)
        DecisionResult EvaluateTimedBlock(RE::Actor *a_actor, const ActorStateData &a_state);

        // Helper: Calculate strafe direction (perpendicular to target)
        RE::NiPoint3 CalculateStrafeDirection(const ActorStateData &a_state);

        // Helper: Calculate optimal flanking direction based on ally positions
        RE::NiPoint3 CalculateFlankingDirection(const ActorStateData &a_state);

        // Helper: Select best decision when multiple have same priority
        DecisionResult SelectBestFromTie(const std::vector<DecisionResult> &a_decisions, const ActorStateData &a_state);

        // Helper: Calculate score for tie-breaking
        float CalculateDecisionScore(const DecisionResult &a_decision, const ActorStateData &a_state);

        // Helper: Log all state data for selected actor (debugging)
        void LogActorState(RE::Actor *a_actor, const ActorStateData &a_state);

        // CombatStyleEnhancer
        CombatStyleEnhancer m_styleEnhancer;
    };
} // namespace CombatAI
