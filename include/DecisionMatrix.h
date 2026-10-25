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

        // Every option this thread's last Evaluate scored, so a failed skill roll can make the next-best choice.
        // Per thread: actors are updated on several threads at once (a shared vector crashed 0.8.0/0.8.1).
        static const std::vector<DecisionResult> &LastCandidates();

        // Damage race against the current target: hits each side still needs (0 = unknown). Winning: it kills
        // in fewer hits than it dies in. Losing: a few more of the target's hits kill it and it isn't winning.
        struct DamageRace
        {
            float toKill = 0.0f;
            float toDie = 0.0f;
            bool winning = false;
            bool losing = false;
        };
        static DamageRace Race(const ActorStateData &a_state);
        // Archers: factor on vanilla's ranged attack roll (0 hold, 1 vanilla, 2 take it) for this moment
        static float RangedShotFactor(const ActorStateData &a_state);
        // An ally stands in the line from this NPC to its target; a_where gets its position
        static bool AllyInLineOfFire(const ActorStateData &a_state, RE::NiPoint3 *a_where = nullptr);
        // Bow, crossbow, staff or a spell in the right hand
        static bool IsRangedFighter(const ActorStateData &a_state);

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
        // Archers and mages: keep away from melee attackers, close in when too far or out of sight
        DecisionResult EvaluateRangedPositioning(RE::Actor *a_actor, const ActorStateData &a_state);

        // Parry (timed bash for parrying incoming attacks)
        DecisionResult EvaluateParry(RE::Actor *a_actor, const ActorStateData &a_state);

        // Timed Block (timed block for blocking incoming attacks)
        DecisionResult EvaluateTimedBlock(RE::Actor *a_actor, const ActorStateData &a_state);

        // Helper: Calculate strafe direction (perpendicular to target)
        RE::NiPoint3 CalculateStrafeDirection(const ActorStateData &a_state);

        // Helper: Calculate optimal flanking direction based on ally positions
        RE::NiPoint3 CalculateFlankingDirection(const ActorStateData &a_state);



        // Helper: Log all state data for selected actor (debugging)
        void LogActorState(RE::Actor *a_actor, const ActorStateData &a_state);

        // CombatStyleEnhancer
        CombatStyleEnhancer m_styleEnhancer;
    };
} // namespace CombatAI
