#include "AttackDefenseFeedbackTracker.h"
#include "ActorUtils.h"
#include "Config.h"
#include "Logger.h"
#include "pch.h"
#include <algorithm>

// Config.h -> SimpleIni.h pulls in <Windows.h>, which defines min/max macros that
// break std::max/std::min below. Undo them.
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace CombatAI
{
    void AttackDefenseFeedbackTracker::RecordAttackAttempt(RE::Actor *a_attacker, RE::Actor *a_target,
                                                           bool a_isPowerAttack, float a_weaponReach,
                                                           bool a_reachFromPrecision, float a_targetDistance,
                                                           float a_targetRadius, float a_attackThreshold)
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

        AttackAttempt attempt;
        attempt.attackerFormID = attackerFormID;
        attempt.targetFormID = targetFormID;
        attempt.isPowerAttack = a_isPowerAttack;
        attempt.attemptTime = std::chrono::steady_clock::now();
        attempt.matchedParry = false;
        attempt.matchedTimedBlock = false;
        attempt.matchedHit = false;
        attempt.weaponReach = a_weaponReach;
        attempt.reachFromPrecision = a_reachFromPrecision;
        attempt.targetDistance = a_targetDistance;
        attempt.targetRadius = a_targetRadius;
        attempt.attackThreshold = a_attackThreshold;

        size_t totalAttempts = 0;
        // Add to recent attempts (keyed by attacker FormID)
        m_recentAttempts.WithWriteLock([&](auto &attemptsMap) {
            auto &attempts = attemptsMap[attackerFormID];
            attempts.push_back(attempt);
            totalAttempts = attempts.size();

            // Keep only recent attempts (limit size)
            if (attempts.size() > MAX_ATTEMPTS_PER_ATTACKER) {
                attempts.erase(attempts.begin());
            }
        });

        // Update attack count in feedback
        m_feedbackData.WithWriteLock([&](auto &feedbackMap) {
            auto &feedback = feedbackMap[attackerFormID];
            feedback.totalAttackCount++;

            // Recalculate rates
            if (feedback.totalAttackCount > 0) {
                feedback.parryRate =
                    static_cast<float>(feedback.parriedAttackCount) / static_cast<float>(feedback.totalAttackCount);
                feedback.timedBlockRate = static_cast<float>(feedback.timedBlockedAttackCount) /
                                          static_cast<float>(feedback.totalAttackCount);
                feedback.hitRate =
                    static_cast<float>(feedback.hitAttackCount) / static_cast<float>(feedback.totalAttackCount);
                feedback.missRate =
                    static_cast<float>(feedback.missedAttackCount) / static_cast<float>(feedback.totalAttackCount);
                feedback.totalDefenseRate = feedback.parryRate + feedback.timedBlockRate;
            }
        });
    }

    void AttackDefenseFeedbackTracker::OnAttackParried(RE::Actor *a_attacker)
    {
        if (!a_attacker) {
            return;
        }

        auto attackerFormIDOpt = ActorUtils::SafeGetFormID(a_attacker);
        if (!attackerFormIDOpt.has_value()) {
            return;
        }

        RE::FormID attackerFormID = attackerFormIDOpt.value();

        bool foundMatch = false;
        // Find the most recent unmatched attack attempt for this attacker
        m_recentAttempts.WithWriteLock([&](auto &attemptsMap) {
            auto it = attemptsMap.find(attackerFormID);
            if (it == attemptsMap.end()) {
                return; // No attempts found for this attacker
            }

            auto &attempts = it->second;

            // Find most recent unmatched attack attempt
            for (auto attemptIt = attempts.rbegin(); attemptIt != attempts.rend(); ++attemptIt) {
                // Parry and timed block are mutually exclusive - can't match if already matched as timed block
                // But allow matching if not matched yet (timed block mod events can override parry matches later)
                if (!attemptIt->matchedTimedBlock) {
                    // Found attempt that hasn't been matched as timed block yet
                    // Allow matching as parry (timed block mod events can override this later if they arrive)
                    bool wasAlreadyParryMatch = attemptIt->matchedParry;
                    attemptIt->matchedParry = true;
                    foundMatch = true;

                    // Update feedback for the attacker
                    m_feedbackData.WithWriteLock([&](auto &feedbackMap) {
                        auto &feedback = feedbackMap[attackerFormID];
                        feedback.lastAttackParried = true;
                        feedback.lastAttackTimedBlocked = false; // Reset timed block flag
                        feedback.lastAttackHit = false;          // Reset hit flag (parry takes precedence)
                        feedback.lastAttackMissed = false;       // Reset miss flag
                        feedback.timeSinceLastParriedAttack = 0.0f;

                        // Only increment if not already matched (avoid double-counting)
                        if (!wasAlreadyParryMatch) {
                            feedback.parriedAttackCount++;
                        }

                        // Recalculate rates
                        if (feedback.totalAttackCount > 0) {
                            feedback.parryRate = static_cast<float>(feedback.parriedAttackCount) /
                                                 static_cast<float>(feedback.totalAttackCount);
                            feedback.timedBlockRate = static_cast<float>(feedback.timedBlockedAttackCount) /
                                                      static_cast<float>(feedback.totalAttackCount);
                            feedback.hitRate = static_cast<float>(feedback.hitAttackCount) /
                                               static_cast<float>(feedback.totalAttackCount);
                            feedback.missRate = static_cast<float>(feedback.missedAttackCount) /
                                                static_cast<float>(feedback.totalAttackCount);
                            feedback.totalDefenseRate = feedback.parryRate + feedback.timedBlockRate;
                        }
                    });

                    break;
                }
            }
        });
    }

    void AttackDefenseFeedbackTracker::OnAttackTimedBlocked(RE::Actor *a_attacker)
    {
        if (!a_attacker) {
            return;
        }

        auto attackerFormIDOpt = ActorUtils::SafeGetFormID(a_attacker);
        if (!attackerFormIDOpt.has_value()) {
            return;
        }

        RE::FormID attackerFormID = attackerFormIDOpt.value();

        // Find the most recent unmatched attack attempt for this attacker
        bool foundMatch = false;
        m_recentAttempts.WithWriteLock([&](auto &attemptsMap) {
            auto it = attemptsMap.find(attackerFormID);
            if (it == attemptsMap.end()) {
                return; // No attempts found for this attacker
            }

            auto &attempts = it->second;

            // Find most recent attack attempt (allow overriding parry match if timed block mod event arrives)
            // Timed block mod events are authoritative - if they fire, it's a timed block, not a parry
            for (auto attemptIt = attempts.rbegin(); attemptIt != attempts.rend(); ++attemptIt) {
                if (!attemptIt->matchedTimedBlock) {
                    // Found attempt that hasn't been matched as timed block yet
                    // Allow overriding parry match if timed block mod event arrives (mod events are authoritative)
                    bool wasParryMatch = attemptIt->matchedParry;
                    attemptIt->matchedTimedBlock = true;
                    foundMatch = true;

                    // Update feedback for the attacker
                    m_feedbackData.WithWriteLock([&](auto &feedbackMap) {
                        auto &feedback = feedbackMap[attackerFormID];
                        feedback.lastAttackParried = false; // Reset parry flag (timed block takes precedence)
                        feedback.lastAttackTimedBlocked = true;
                        feedback.lastAttackHit = false;    // Reset hit flag (timed block takes precedence)
                        feedback.lastAttackMissed = false; // Reset miss flag
                        feedback.timeSinceLastTimedBlockedAttack = 0.0f;
                        feedback.timedBlockedAttackCount++;

                        // If this was previously matched as parry, decrement parry count
                        if (wasParryMatch) {
                            feedback.parriedAttackCount = std::max(0, feedback.parriedAttackCount - 1);
                        }

                        // Recalculate rates
                        if (feedback.totalAttackCount > 0) {
                            feedback.parryRate = static_cast<float>(feedback.parriedAttackCount) /
                                                 static_cast<float>(feedback.totalAttackCount);
                            feedback.timedBlockRate = static_cast<float>(feedback.timedBlockedAttackCount) /
                                                      static_cast<float>(feedback.totalAttackCount);
                            feedback.hitRate = static_cast<float>(feedback.hitAttackCount) /
                                               static_cast<float>(feedback.totalAttackCount);
                            feedback.missRate = static_cast<float>(feedback.missedAttackCount) /
                                                static_cast<float>(feedback.totalAttackCount);
                            feedback.totalDefenseRate = feedback.parryRate + feedback.timedBlockRate;
                        }
                    });

                    break;
                }
            }
        });
    }

    void AttackDefenseFeedbackTracker::OnAttackHit(RE::Actor *a_attacker, RE::Actor *a_target)
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

        // Sample the target's current health fraction (for damage-momentum). -1 = unknown.
        float currentHealthPct = -1.0f;
        auto avo = ActorUtils::SafeAsActorValueOwner(a_target);
        if (avo) {
            float mx = a_target->GetActorValueMax(RE::ActorValue::kHealth);
            float cur = avo->GetActorValue(RE::ActorValue::kHealth);
            if (mx > 0.0f) {
                currentHealthPct = cur / mx;
            }
        }

        // Damage momentum: sample on EVERY confirmed hit (the TESHitEvent already proved
        // the hit landed), independent of the attempt-matching machinery below. That
        // matching frequently fails to resolve for sprint/power attacks, which would
        // starve the momentum signal (observed: max Hit count = 1 across a whole fight).
        // Fold health removed since our previous hit on THIS target into an EWMA; the
        // first hit on a target just primes the baseline (no delta yet).
        if (currentHealthPct >= 0.0f) {
            const auto &dm = Config::GetInstance().GetDamageMomentum();
            m_feedbackData.WithWriteLock([&](auto &feedbackMap) {
                auto &feedback = feedbackMap[attackerFormID];
                if (feedback.lastTargetFormID == targetFormID) {
                    float delta = feedback.lastTargetHealthPercent - currentHealthPct;
                    if (delta < 0.0f) {
                        delta = 0.0f; // regen / heal
                    }
                    if (delta > 0.5f) {
                        delta = 0.5f; // cap outliers (group-fight overcount)
                    }
                    feedback.recentDamageFraction =
                        dm.ewmaAlpha * delta + (1.0f - dm.ewmaAlpha) * feedback.recentDamageFraction;
                    feedback.hasMomentumData = true;
                }
                feedback.lastTargetFormID = targetFormID;
                feedback.lastTargetHealthPercent = currentHealthPct;
            });
        }

        // Find the most recent unmatched attack attempt for this attacker-target pair
        bool foundMatch = false;
        m_recentAttempts.WithWriteLock([&](auto &attemptsMap) {
            auto it = attemptsMap.find(attackerFormID);
            if (it == attemptsMap.end()) {
                return; // No attempts found for this attacker
            }

            auto &attempts = it->second;

            // Find most recent unmatched attempt matching this target
            // Hits can only match if not already matched as parry/timed block
            for (auto attemptIt = attempts.rbegin(); attemptIt != attempts.rend(); ++attemptIt) {
                if (attemptIt->targetFormID == targetFormID && !attemptIt->matchedParry &&
                    !attemptIt->matchedTimedBlock && !attemptIt->matchedHit) {
                    // Found unmatched attempt for this target
                    attemptIt->matchedHit = true;
                    foundMatch = true;

                    // Update feedback for the attacker
                    m_feedbackData.WithWriteLock([&](auto &feedbackMap) {
                        auto &feedback = feedbackMap[attackerFormID];
                        feedback.lastAttackHit = true;
                        feedback.lastAttackMissed = false;
                        feedback.lastAttackParried = false;      // Reset parry flag
                        feedback.lastAttackTimedBlocked = false; // Reset timed block flag
                        feedback.timeSinceLastHitAttack = 0.0f;
                        feedback.hitAttackCount++;

                        // Recalculate rates
                        if (feedback.totalAttackCount > 0) {
                            feedback.parryRate = static_cast<float>(feedback.parriedAttackCount) /
                                                 static_cast<float>(feedback.totalAttackCount);
                            feedback.timedBlockRate = static_cast<float>(feedback.timedBlockedAttackCount) /
                                                      static_cast<float>(feedback.totalAttackCount);
                            feedback.hitRate = static_cast<float>(feedback.hitAttackCount) /
                                               static_cast<float>(feedback.totalAttackCount);
                            feedback.missRate = static_cast<float>(feedback.missedAttackCount) /
                                                static_cast<float>(feedback.totalAttackCount);
                            feedback.totalDefenseRate = feedback.parryRate + feedback.timedBlockRate;
                        }
                    });

                    break;
                }
            }
        });
    }

    void AttackDefenseFeedbackTracker::Update(float a_deltaTime)
    {
        auto now = std::chrono::steady_clock::now();

        // Detect misses: unmatched attempts older than MISS_DETECTION_TIME
        static constexpr float MISS_DETECTION_TIME =
            1.5f; // 1.5 seconds - if no hit/parry/timed block by then, consider it a miss

        m_recentAttempts.WithWriteLock([&](auto &attemptsMap) {
            for (auto it = attemptsMap.begin(); it != attemptsMap.end(); ++it) {
                RE::FormID attackerFormID = it->first;
                auto &attempts = it->second;

                for (auto &attempt : attempts) {
                    auto age =
                        std::chrono::duration_cast<std::chrono::milliseconds>(now - attempt.attemptTime).count() /
                        1000.0f;

                    // If attempt is old enough and hasn't been matched, it's a miss
                    if (age >= MISS_DETECTION_TIME && !attempt.matchedParry && !attempt.matchedTimedBlock &&
                        !attempt.matchedHit) {
                        if (Config::GetInstance().IsDebugTarget(attackerFormID)) {
                            LOG_DEBUG("Unmatched attack timeout: actor=0x{:08X} target=0x{:08X} power={} "
                                      "reachSource={} reach={:.1f} targetRadius={:.1f} distance={:.1f} "
                                      "threshold={:.1f} overshoot={:.1f}",
                                      attackerFormID, attempt.targetFormID, attempt.isPowerAttack,
                                      attempt.reachFromPrecision ? "Precision" : "fallback", attempt.weaponReach,
                                      attempt.targetRadius, attempt.targetDistance, attempt.attackThreshold,
                                      attempt.targetDistance - attempt.attackThreshold);
                        }
                        // Mark as matched to avoid double-counting
                        attempt.matchedHit = true; // Use matchedHit flag to mark as processed

                        // Update feedback for the attacker
                        m_feedbackData.WithWriteLock([&](auto &feedbackMap) {
                            auto &feedback = feedbackMap[attackerFormID];
                            feedback.lastAttackMissed = true;
                            feedback.lastAttackHit = false;
                            feedback.timeSinceLastMissedAttack = 0.0f;
                            feedback.missedAttackCount++;

                            // Recalculate rates
                            if (feedback.totalAttackCount > 0) {
                                feedback.parryRate = static_cast<float>(feedback.parriedAttackCount) /
                                                     static_cast<float>(feedback.totalAttackCount);
                                feedback.timedBlockRate = static_cast<float>(feedback.timedBlockedAttackCount) /
                                                          static_cast<float>(feedback.totalAttackCount);
                                feedback.hitRate = static_cast<float>(feedback.hitAttackCount) /
                                                   static_cast<float>(feedback.totalAttackCount);
                                feedback.missRate = static_cast<float>(feedback.missedAttackCount) /
                                                    static_cast<float>(feedback.totalAttackCount);
                                feedback.totalDefenseRate = feedback.parryRate + feedback.timedBlockRate;
                            }
                        });
                    }
                }
            }
        });

        // Clean up old attempts
        m_recentAttempts.WithWriteLock([&](auto &attemptsMap) {
            for (auto it = attemptsMap.begin(); it != attemptsMap.end(); ++it) {
                auto &attempts = it->second;
                // Remove old attempts
                attempts.erase(std::remove_if(attempts.begin(), attempts.end(),
                                              [&](const AttackAttempt &attempt) {
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

        // Update feedback timers
        const float decaySeconds = Config::GetInstance().GetDamageMomentum().momentumDecaySeconds;
        m_feedbackData.WithWriteLock([&](auto &feedbackMap) {
            for (auto it = feedbackMap.begin(); it != feedbackMap.end(); ++it) {
                auto &fb = it->second;
                fb.timeSinceLastParriedAttack += a_deltaTime;
                fb.timeSinceLastTimedBlockedAttack += a_deltaTime;
                fb.timeSinceLastHitAttack += a_deltaTime;
                fb.timeSinceLastMissedAttack += a_deltaTime;

                // Momentum fades toward 0 with no hits (linear over decaySeconds).
                if (fb.recentDamageFraction > 0.0f && decaySeconds > 0.0f) {
                    fb.recentDamageFraction -= (a_deltaTime / decaySeconds) * fb.recentDamageFraction;
                    if (fb.recentDamageFraction < 0.0001f) {
                        fb.recentDamageFraction = 0.0f;
                    }
                }
            }
        });
    }

    AttackDefenseFeedbackTracker::AttackDefenseFeedback AttackDefenseFeedbackTracker::GetFeedback(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return AttackDefenseFeedback();
        }

        auto formIDOpt = ActorUtils::SafeGetFormID(a_actor);
        if (!formIDOpt.has_value()) {
            return AttackDefenseFeedback();
        }

        auto feedbackOpt = m_feedbackData.Find(formIDOpt.value());
        if (feedbackOpt.has_value()) {
            return feedbackOpt.value();
        }

        return AttackDefenseFeedback();
    }
} // namespace CombatAI
