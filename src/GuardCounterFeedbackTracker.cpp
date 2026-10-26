#include "GuardCounterFeedbackTracker.h"
#include "ActorUtils.h"
#include "Logger.h"
#include "pch.h"
#include <algorithm>

namespace CombatAI
{
    void GuardCounterFeedbackTracker::RecordGuardCounterAttempt(RE::Actor *a_attacker, RE::Actor *a_target)
    {
        if (!a_attacker || !a_target) {
            return;
        }

        auto attackerFormIDOpt = ActorUtils::SafeGetFormID(a_attacker);
        auto targetFormIDOpt = ActorUtils::SafeGetFormID(a_target);

        if (!attackerFormIDOpt.has_value() || !targetFormIDOpt.has_value()) {
            return;
        }

        RE::FormID attackerFormID = attackerFormIDOpt.value();
        RE::FormID targetFormID = targetFormIDOpt.value();

        GuardCounterAttempt attempt;
        attempt.attackerFormID = attackerFormID;
        attempt.targetFormID = targetFormID;
        attempt.attemptTime = std::chrono::steady_clock::now();
        attempt.matchedHit = false;

        // Add to recent attempts (keyed by attacker FormID)
        m_recentAttempts.WithWriteLock([&](auto &attemptsMap) {
            auto &attempts = attemptsMap[attackerFormID];
            attempts.push_back(attempt);

            // Keep only recent attempts (limit size)
            if (attempts.size() > MAX_ATTEMPTS_PER_ATTACKER) {
                attempts.erase(attempts.begin());
            }
        });

        // Update attempt count in feedback
        m_feedbackData.WithWriteLock([&](auto &feedbackMap) {
            auto &feedback = feedbackMap[attackerFormID];
            feedback.guardCounterAttemptCount++;
            feedback.timeSinceLastGuardCounterAttempt = 0.0f;

            // Recalculate success rate
            if (feedback.guardCounterAttemptCount > 0) {
                feedback.guardCounterSuccessRate = static_cast<float>(feedback.guardCounterSuccessCount) /
                                                   static_cast<float>(feedback.guardCounterAttemptCount);
            }
        });
    }

    void GuardCounterFeedbackTracker::OnGuardCounterHit(RE::Actor *a_attacker)
    {
        if (!a_attacker) {
            return;
        }

        auto attackerFormIDOpt = ActorUtils::SafeGetFormID(a_attacker);
        if (!attackerFormIDOpt.has_value()) {
            return;
        }

        RE::FormID attackerFormID = attackerFormIDOpt.value();

        // Find the most recent unmatched guard counter attempt
        bool foundMatch = false;
        m_recentAttempts.WithWriteLock([&](auto &attemptsMap) {
            auto it = attemptsMap.find(attackerFormID);
            if (it == attemptsMap.end()) {
                return; // No attempts found for this attacker
            }

            auto &attempts = it->second;

            // Find most recent unmatched attempt
            for (auto attemptIt = attempts.rbegin(); attemptIt != attempts.rend(); ++attemptIt) {
                if (!attemptIt->matchedHit) {
                    // Found unmatched attempt - mark as matched
                    attemptIt->matchedHit = true;
                    foundMatch = true;

                    // Update feedback for the attacker
                    m_feedbackData.WithWriteLock([&](auto &feedbackMap) {
                        auto &feedback = feedbackMap[attackerFormID];
                        feedback.lastGuardCounterSuccess = true;
                        feedback.timeSinceLastGuardCounterAttempt = 0.0f;
                        feedback.guardCounterSuccessCount++;

                        // Recalculate success rate
                        if (feedback.guardCounterAttemptCount > 0) {
                            feedback.guardCounterSuccessRate = static_cast<float>(feedback.guardCounterSuccessCount) /
                                                               static_cast<float>(feedback.guardCounterAttemptCount);
                        }
                    });

                    break;
                }
            }
        });
    }

    void GuardCounterFeedbackTracker::Update(float a_deltaTime)
    {
        // Update feedback timers
        m_feedbackData.WithWriteLock([&](auto &feedbackMap) {
            for (auto it = feedbackMap.begin(); it != feedbackMap.end(); ++it) {
                it->second.timeSinceLastGuardCounterAttempt += a_deltaTime;
            }
        });

        // Clean up old attempts
        auto now = std::chrono::steady_clock::now();
        m_recentAttempts.WithWriteLock([&](auto &attemptsMap) {
            for (auto it = attemptsMap.begin(); it != attemptsMap.end(); ++it) {
                auto &attempts = it->second;
                // Remove old attempts
                attempts.erase(std::remove_if(attempts.begin(), attempts.end(),
                                              [&](const GuardCounterAttempt &attempt) {
                                                  auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                                 now - attempt.attemptTime)
                                                                 .count() /
                                                             1000.0f;
                                                  return age > MAX_ATTEMPT_AGE;
                                              }),
                               attempts.end());
            }

            // Remove empty entries
            auto mapIt = attemptsMap.begin();
            while (mapIt != attemptsMap.end()) {
                if (mapIt->second.empty()) {
                    mapIt = attemptsMap.erase(mapIt);
                } else {
                    ++mapIt;
                }
            }
        });
    }

    GuardCounterFeedbackTracker::GuardCounterFeedback GuardCounterFeedbackTracker::GetFeedback(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return GuardCounterFeedback();
        }

        auto formIDOpt = ActorUtils::SafeGetFormID(a_actor);
        if (!formIDOpt.has_value()) {
            return GuardCounterFeedback();
        }

        auto feedbackOpt = m_feedbackData.Find(formIDOpt.value());
        if (feedbackOpt.has_value()) {
            return feedbackOpt.value();
        }

        return GuardCounterFeedback();
    }
} // namespace CombatAI
