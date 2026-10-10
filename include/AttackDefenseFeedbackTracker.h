#pragma once

#include "ThreadSafeMap.h"
#include "pch.h"
#include <chrono>

namespace CombatAI
{
    // Tracks when NPC's attacks are parried or timed blocked by the target
    // This allows NPCs to adapt their behavior (use feints, vary timing, etc.)
    class AttackDefenseFeedbackTracker
    {
      public:
        static AttackDefenseFeedbackTracker &GetInstance()
        {
            static AttackDefenseFeedbackTracker instance;
            return instance;
        }

        AttackDefenseFeedbackTracker(const AttackDefenseFeedbackTracker &) = delete;
        AttackDefenseFeedbackTracker &operator=(const AttackDefenseFeedbackTracker &) = delete;

        // Record an attack attempt (when NPC starts attacking)
        void RecordAttackAttempt(RE::Actor *a_attacker, RE::Actor *a_target, bool a_isPowerAttack,
                                 float a_weaponReach, bool a_reachFromPrecision, float a_targetDistance,
                                 float a_targetRadius, float a_attackThreshold);

        // Handle EldenParry mod callback event (called when attacker's attack is parried)
        void OnAttackParried(RE::Actor *a_attacker);

        // Handle Simple Timed Block mod callback event (called when attacker's attack is timed blocked)
        void OnAttackTimedBlocked(RE::Actor *a_attacker);

        // Handle successful hit (called when attacker's attack hits the target)
        void OnAttackHit(RE::Actor *a_attacker, RE::Actor *a_target);

        // Update timers and clean up old attempts
        void Update(float a_deltaTime);

        // Get feedback data for an actor
        struct AttackDefenseFeedback
        {
            bool lastAttackParried = false;            // Whether last attack was parried
            bool lastAttackTimedBlocked = false;       // Whether last attack was timed blocked
            bool lastAttackHit = false;                // Whether last attack successfully hit the target
            bool lastAttackMissed = false;             // Whether last attack missed (no hit, not parried/timed blocked)
            float timeSinceLastParriedAttack = 999.0f; // Time since last parried attack
            float timeSinceLastTimedBlockedAttack = 999.0f; // Time since last timed blocked attack
            float timeSinceLastHitAttack = 999.0f;          // Time since last successful hit
            float timeSinceLastMissedAttack = 999.0f;       // Time since last missed attack
            int parriedAttackCount = 0;                     // Number of attacks that were parried
            int timedBlockedAttackCount = 0;                // Number of attacks that were timed blocked
            int hitAttackCount = 0;                         // Number of attacks that successfully hit
            int missedAttackCount = 0;                      // Number of attacks that missed
            int totalAttackCount = 0;                       // Total number of attacks attempted
            float parryRate = 0.0f;                         // Percentage of attacks parried (0.0-1.0)
            float timedBlockRate = 0.0f;                    // Percentage of attacks timed blocked (0.0-1.0)
            float hitRate = 0.0f;                           // Percentage of attacks that hit (0.0-1.0)
            float missRate = 0.0f;                          // Percentage of attacks that missed (0.0-1.0)
            float totalDefenseRate = 0.0f;                  // Combined parry + timed block rate

            // Damage momentum: how much of the target's health this NPC's recent hits
            // have been removing (EWMA fraction of target max health), used to scale
            // aggression. Health-delta measured at each hit.
            float recentDamageFraction = 0.0f;   // Smoothed fraction of target max health per hit
            bool hasMomentumData = false;         // True once at least one hit-to-hit delta measured
            float lastTargetHealthPercent = 1.0f; // Target health% at our previous hit
            RE::FormID lastTargetFormID = 0;      // Target of our previous hit (reset delta on change)
        };
        AttackDefenseFeedback GetFeedback(RE::Actor *a_actor);

      private:
        AttackDefenseFeedbackTracker() = default;
        ~AttackDefenseFeedbackTracker() = default;

        // Structure to track an attack attempt
        struct AttackAttempt
        {
            RE::FormID attackerFormID; // Actor who is attacking
            RE::FormID targetFormID;   // Actor being attacked
            bool isPowerAttack;
            std::chrono::steady_clock::time_point attemptTime;
            bool matchedParry = false;      // Whether this attack was matched with a parry event
            bool matchedTimedBlock = false; // Whether this attack was matched with a timed block event
            bool matchedHit = false;        // Whether this attack was matched with a hit event
            float weaponReach = 0.0f;
            bool reachFromPrecision = false;
            float targetDistance = 0.0f;
            float targetRadius = 0.0f;
            float attackThreshold = 0.0f;
        };

        // Track recent attack attempts (keyed by attacker FormID)
        ThreadSafeMap<RE::FormID, std::vector<AttackAttempt>> m_recentAttempts;

        // Track feedback per actor (keyed by attacker FormID)
        ThreadSafeMap<RE::FormID, AttackDefenseFeedback> m_feedbackData;

        // Maximum age for attack attempts (clean up old attempts)
        static constexpr float MAX_ATTEMPT_AGE = 2.0f;          // 2 seconds
        static constexpr size_t MAX_ATTEMPTS_PER_ATTACKER = 10; // Keep max 10 recent attempts per attacker
    };
} // namespace CombatAI
