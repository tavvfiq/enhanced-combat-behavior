#pragma once

#include "ThreadSafeMap.h"
#include "pch.h"
#include <chrono>

namespace CombatAI
{
    // Tracks guard counter attempts and matches them with attack outcomes
    // Guard counter is activated by EldenCounter mod when NPC successfully blocks
    class GuardCounterFeedbackTracker
    {
      public:
        static GuardCounterFeedbackTracker &GetInstance()
        {
            static GuardCounterFeedbackTracker instance;
            return instance;
        }

        GuardCounterFeedbackTracker(const GuardCounterFeedbackTracker &) = delete;
        GuardCounterFeedbackTracker &operator=(const GuardCounterFeedbackTracker &) = delete;

        // Record a power attack attempt during guard counter window
        void RecordGuardCounterAttempt(RE::Actor *a_attacker, RE::Actor *a_target);

        // Handle successful hit from guard counter attack
        void OnGuardCounterHit(RE::Actor *a_attacker);

        // Update timers and clean up old attempts
        void Update(float a_deltaTime);

        // Get feedback data for an actor
        struct GuardCounterFeedback
        {
            bool lastGuardCounterSuccess = false;            // Whether last guard counter attempt was successful
            float timeSinceLastGuardCounterAttempt = 999.0f; // Time since last guard counter attempt
            int guardCounterSuccessCount = 0;                // Number of successful guard counters (hit)
            int guardCounterAttemptCount = 0;                // Total number of guard counter attempts
            int guardCounterFailedCount = 0;                 // Number of failed guard counters (missed/blocked)
            float guardCounterSuccessRate = 0.0f;       // Success rate (0.0-1.0)
        };
        GuardCounterFeedback GetFeedback(RE::Actor *a_actor);

      private:
        GuardCounterFeedbackTracker() = default;
        ~GuardCounterFeedbackTracker() = default;

        // Structure to track a guard counter attempt
        struct GuardCounterAttempt
        {
            RE::FormID attackerFormID; // Actor who attempted guard counter
            RE::FormID targetFormID;   // Actor being attacked
            std::chrono::steady_clock::time_point attemptTime;
            bool matchedHit = false; // Whether this attempt was matched with a hit event
        };

        // Track recent guard counter attempts (keyed by attacker FormID)
        ThreadSafeMap<RE::FormID, std::vector<GuardCounterAttempt>> m_recentAttempts;

        // Track feedback per actor (keyed by attacker FormID)
        ThreadSafeMap<RE::FormID, GuardCounterFeedback> m_feedbackData;

        // Maximum age for guard counter attempts (clean up old attempts)
        static constexpr float MAX_ATTEMPT_AGE = 2.0f;          // 2 seconds
        static constexpr size_t MAX_ATTEMPTS_PER_ATTACKER = 10; // Keep max 10 recent attempts per attacker
    };
} // namespace CombatAI
