#pragma once

#include "ActorStateData.h"
#include "DecisionResult.h"
#include "ThreadSafeMap.h"
#include "pch.h"

// Forward declaration
namespace CombatAI
{
    class PrecisionIntegration;
}

namespace CombatAI
{
    // Gathers state data for actors and their targets
    class ActorStateObserver
    {
      public:
        ActorStateObserver() = default;
        ~ActorStateObserver() = default;

        // Gather complete state data for an actor
        ActorStateData GatherState(RE::Actor *a_actor, float a_deltaTime);

        // Advance the internal time counter (call once per frame from
        // CombatDirector::Update)
        void Update(float a_deltaTime);

        // Cleanup cached data for an actor
        void Cleanup(RE::Actor *a_actor);

        // Evict all per-actor caches by FormID. Called when an actor leaves combat / is
        // torn down so a recycled temporary FormID (0xFF...) does not inherit stale state.
        void EvictActor(RE::FormID a_formID);

        // Notify that an action was executed (for temporal tracking)
        void NotifyActionExecuted(RE::Actor *a_actor, ActionType a_action);
        // Starts the anti-spam action timer when an attack/bash actually starts (any thread)
        void NotifySwingStarted(RE::Actor *a_actor);

      private:
        // Gather self state
        SelfState GatherSelfState(RE::Actor *a_actor);

        // Gather target state
        TargetState GatherTargetState(RE::Actor *a_actor, RE::Actor *a_target);

        // Gather combat context (multiple enemies, etc.)
        CombatContext GatherCombatContext(RE::Actor *a_actor, float a_currentTime, float a_weaponReach);

        // Scan for nearby actors (enemies and allies) - expensive operation, done in
        // one pass
        void ScanForNearbyActors(RE::Actor *a_actor, CombatContext &a_context, RE::Actor *a_primaryTarget);

        // Helper: Get actor value percentage
        float GetActorValuePercent(RE::Actor *a_actor, RE::ActorValue a_value);

        // Helper: Check if target is power attacking
        bool IsPowerAttacking(RE::Actor *a_target);

        // Helper: Get weapon reach (approximation)
        float GetWeaponReach(RE::Actor *a_actor, bool *a_fromPrecision = nullptr);

        // Recompute distance-dependent fields (primary-target distance + range
        // category / isInAttackRange) from the LIVE target position. The rest of the
        // combat context is cached for several seconds, but range must stay fresh or
        // NPCs act on a stale distance (e.g. think a point-blank target is far away).
        void UpdateRangeCategory(RE::Actor *a_actor, CombatContext &a_context, float a_weaponReach);

        // Gather temporal state (time-based tracking)
        TemporalState GatherTemporalState(RE::Actor *a_actor, RE::Actor *a_target, float a_deltaTime,
                                          const ActorStateData &a_state);

        // Caching for combat context (expensive to calculate)
        struct CachedCombatContext
        {
            CombatContext context;
            float lastUpdateTime = 0.0f;
        };
        // Thread-safe to prevent crashes from concurrent access
        ThreadSafeMap<RE::FormID, CachedCombatContext> m_combatContextCache;
        // How long the expensive nearby-actor scan (enemy/ally counts, ally positions)
        // is cached. Range/distance is refreshed every gather via UpdateRangeCategory.
        static constexpr float COMBAT_CONTEXT_UPDATE_INTERVAL = 5.0f;

        // RequestDetectionLevel (player stealth detection) is expensive and crash-prone,
        // so cache it per observer and refresh at most every DETECTION_UPDATE_INTERVAL.
        struct CachedDetection
        {
            std::int32_t level = 0;
            float lastUpdateTime = -999.0f;
        };
        ThreadSafeMap<RE::FormID, CachedDetection> m_detectionCache;
        static constexpr float DETECTION_UPDATE_INTERVAL = 0.5f;

        // Temporal state tracking per actor
        struct ActorTemporalData
        {
            // Self temporal tracking
            float timeSinceLastAttack = 999.0f;
            float timeSinceLastDodge = 999.0f;
            float timeSinceLastEvasion = 999.0f;
            float timeSinceLastAction = 999.0f;
            float blockingDuration = 0.0f;
            float attackingDuration = 0.0f;
            float idleDuration = 0.0f;
            float timeSinceLastPowerAttack = 999.0f;
            float timeSinceLastSprintAttack = 999.0f;
            float timeSinceLastBash = 999.0f;
            float timeSinceLastFeint = 999.0f;
            // Time spent retreating since its health last rose, and its lowest health since (cornered rule)
            bool retreating = false;
            float retreatTime = 0.0f;
            float retreatHealth = 0.0f;

            // Parry feedback data
            bool lastParrySuccess = false;            // Whether last parry attempt was successful
            float lastParryEstimatedDuration = 0.0f;  // Estimated duration used for last parry attempt
            float timeSinceLastParryAttempt = 999.0f; // Time since last parry attempt
            int parrySuccessCount = 0;                // Number of successful parries (for learning)
            int parryAttemptCount = 0;                // Total number of parry attempts (for learning)

            // Timed block feedback data
            bool lastTimedBlockSuccess = false;            // Whether last timed block attempt was successful
            float lastTimedBlockEstimatedDuration = 0.0f;  // Estimated duration used for last timed block attempt
            float timeSinceLastTimedBlockAttempt = 999.0f; // Time since last timed block attempt
            int timedBlockSuccessCount = 0;                // Number of successful timed blocks (for learning)
            int timedBlockAttemptCount = 0;                // Total number of timed block attempts (for learning)

            // Guard Counter feedback data
            bool lastGuardCounterSuccess = false;            // Whether last guard counter attempt was successful
            float timeSinceLastGuardCounterAttempt = 999.0f; // Time since last guard counter attempt
            int guardCounterSuccessCount = 0;                // Number of successful guard counters (hit)
            int guardCounterAttemptCount = 0;                // Total number of guard counter attempts
            int guardCounterFailedCount = 0;                 // Number of failed guard counters (missed/blocked)
            int guardCounterMissedOpportunityCount = 0; // Number of times guard counter window expired without attempt
            float guardCounterSuccessRate = 0.0f;       // Success rate (0.0-1.0)

            // Attack defense feedback data (when NPC's attacks are parried/timed
            // blocked)
            bool lastAttackParried = false;      // Whether last attack was parried
            bool lastAttackTimedBlocked = false; // Whether last attack was timed blocked
            bool lastAttackHit = false;
            bool lastAttackMissed = false;
            float timeSinceLastParriedAttack = 999.0f;      // Time since last parried attack
            float timeSinceLastTimedBlockedAttack = 999.0f; // Time since last timed blocked attack
            float timeSinceLastHitAttack = 999.0f;
            float timeSinceLastMissedAttack = 999.0f;
            int parriedAttackCount = 0;      // Number of attacks that were parried
            int timedBlockedAttackCount = 0; // Number of attacks that were timed blocked
            int hitAttackCount = 0;
            int missedAttackCount = 0;
            int totalAttackCount = 0;    // Total number of attacks attempted
            float parryRate = 0.0f;      // Percentage of attacks parried (0.0-1.0)
            float timedBlockRate = 0.0f; // Percentage of attacks timed blocked (0.0-1.0)
            float hitRate = 0.0f;
            float missRate = 0.0f;
            float totalDefenseRate = 0.0f; // Combined parry + timed block rate

            // Damage momentum
            float recentDamageFraction = 0.0f;
            bool hasMomentumData = false;

            // Previous states for duration tracking
            bool wasBlocking = false;
            bool wasAttacking = false;
            bool wasIdle = false;
            RE::ATTACK_STATE_ENUM previousAttackState = RE::ATTACK_STATE_ENUM::kNone;
        };

        // Target temporal tracking per actor-target pair
        struct TargetTemporalData
        {
            RE::FormID targetFormID = 0;
            float timeSinceLastAttack = 999.0f;
            float blockingDuration = 0.0f;
            float recentBlockCommitments = 0.0f;
            float castingDuration = 0.0f;
            float drawingDuration = 0.0f;
            float attackingDuration = 0.0f;
            float idleDuration = 0.0f;
            float timeSinceLastPowerAttack = 999.0f;

            // Parry timing data
            float attackStartTime = -1.0f;        // When current attack started (relative
                                                  // time, -1 if not attacking)
            float estimatedAttackDuration = 0.0f; // Estimated total duration of current attack
            float timeUntilAttackHits = 999.0f;   // Estimated time until attack hits (999
                                                  // if not attacking or already hit)
            bool isPowerAttack = false;           // Track if current attack is a power attack
            std::uint32_t swingCount = 0;         // One id per swing (attackStartTime is elapsed time, not an id)

            // Previous states for duration tracking
            bool wasBlocking = false;
            bool wasAttacking = false;
            bool wasCasting = false;
            bool wasDrawing = false;
            bool wasIdle = false;
            RE::ATTACK_STATE_ENUM previousAttackState = RE::ATTACK_STATE_ENUM::kNone;
        };

        // Helper: Estimate attack duration based on weapon and attack type
        float EstimateAttackDuration(RE::Actor *a_target, bool a_isPowerAttack);

        // Thread-safe temporal state storage (keyed by FormID)
        ThreadSafeMap<RE::FormID, ActorTemporalData> m_actorTemporalData;
        ThreadSafeMap<RE::FormID, TargetTemporalData> m_targetTemporalData; // Keyed by target FormID, but we'll use
                                                                            // actor-target pair

        // Monotonic time counter — advanced exactly once per frame by Update()
        float m_currentTime = 0.0f;

        // Backstop sweep for the timestamped caches (detection, combat context): drops
        // entries for actors that unloaded without passing through the combat-reject
        // eviction path (e.g. whole-cell unload). Lazy EvictActor covers the common case.
        void PruneStaleCaches();
        float m_cacheSweepTimer = 0.0f;
        static constexpr float CACHE_SWEEP_INTERVAL = 10.0f; // how often to sweep
        static constexpr float CACHE_STALE_THRESHOLD = 15.0f; // evict entries older than this
    };
} // namespace CombatAI
