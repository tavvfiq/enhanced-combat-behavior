#include "Humanizer.h"
#include "ActorUtils.h"
#include "pch.h"
#include <random>

namespace CombatAI
{
    namespace
    {
        // Thread-local random number generators (one per thread)
        thread_local std::random_device g_rd;
        thread_local std::mt19937 g_gen(g_rd());
    } // namespace

    bool Humanizer::CanReact(RE::Actor *a_actor, RE::FormID a_formID, float a_deltaTime)
    {
        if (!a_actor || a_formID == RE::FormID(0)) {
            return false;
        }

        // Only process actors in combat - clean up if not in combat
        if (!ActorUtils::SafeIsInCombat(a_actor)) {
            // Remove reaction state if actor left combat
            m_reactionStates.Erase(a_formID);
            return false;
        }

        // Ensure a reaction state exists and check whether the delay still needs to be
        // initialized. The read-modify-write is done atomically under the map's lock.
        bool needsInit = false;
        m_reactionStates.ModifyOrCreate(a_formID,
                                        [&](ActorReactionState &state) { needsInit = (state.reactionDelay == 0.0f); });

        // Initialize delay if not set. InitializeReactionDelay locks the same map, so it
        // must run outside the callback above to avoid a recursive (deadlocking) lock.
        if (needsInit) {
            InitializeReactionDelay(a_actor);
        }

        // Advance the reaction timer and evaluate readiness atomically under the lock.
        // If the entry was removed in the meantime, treat the actor as unable to react.
        bool canReact = false;
        m_reactionStates.Modify(a_formID, [&](ActorReactionState &state) {
            // If already able to react, keep it (reset happens after the action executes).
            if (state.canReact) {
                canReact = true;
                return;
            }

            // Update timer (convert seconds to milliseconds) and check if the delay passed.
            state.reactionTimer += a_deltaTime * 1000.0f;
            if (state.reactionTimer >= state.reactionDelay) {
                state.canReact = true;
                canReact = true;
            }
        });

        return canReact;
    }

    void Humanizer::ResetReactionState(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return;
        }

        // Get FormID safely - use FormID as key instead of raw pointer
        auto formIDOpt = ActorUtils::SafeGetFormID(a_actor);
        if (!formIDOpt.has_value()) {
            return; // Can't get FormID, actor is invalid
        }
        RE::FormID formID = formIDOpt.value();

        // Validate FormID before using it
        if (formID == RE::FormID(0)) {
            return; // Invalid FormID
        }

        // Reset timer and delay atomically; will be re-initialized on next CanReact call
        m_reactionStates.Modify(formID, [](ActorReactionState &state) {
            state.reactionTimer = 0.0f;
            state.reactionDelay = 0.0f;
            state.canReact = false;
        });
    }

    bool Humanizer::ShouldMakeMistake(RE::Actor *a_actor, ActionType a_action)
    {
        if (!a_actor) {
            return false;
        }

        // Get level safely using wrapper
        std::uint16_t level = ActorUtils::SafeGetLevel(a_actor);
        float mistakeChance = CalculateMistakeChance(level, a_action);

        if (mistakeChance <= 0.0f) {
            return false;
        }

        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        return dist(g_gen) < mistakeChance;
    }

    bool Humanizer::IsOnCooldown(RE::FormID a_formID, ActionType a_action)
    {
        // Validate FormID before using it
        if (a_formID == RE::FormID(0)) {
            return true; // Invalid FormID - safe default (on cooldown)
        }

        // Read the cooldown atomically. Holding the outer map's lock for the lookup
        // prevents another thread from erasing the actor entry (and thus the nested
        // cooldown map) while we are reading from it.
        // Forward dodge and strafe share the ordinary dodge cooldown.
        const ActionType cooldownKey =
            (a_action == ActionType::Strafe || a_action == ActionType::ForwardDodge) ? ActionType::Dodge : a_action;
        bool onCooldown = false;
        m_cooldownStates.Modify(a_formID, [&](ActorCooldownState &cooldownState) {
            auto cooldownOpt = cooldownState.cooldowns.Find(cooldownKey);
            onCooldown = cooldownOpt.has_value() && cooldownOpt.value() > 0.0f;
        });

        return onCooldown;
    }

    float Humanizer::GetCooldownForAction(ActionType a_action) const
    {
        switch (a_action) {
        case ActionType::Bash:
            return m_config.bashCooldownSeconds;
        case ActionType::Dodge:
        case ActionType::ForwardDodge:
            // Strafe uses dodge cooldown (TK Dodge system)
            return m_config.dodgeCooldownSeconds;
        case ActionType::Jump:
            return m_config.jumpCooldownSeconds;
        case ActionType::TimedBlock:
            // Briefly preserve the block animation before a movement decision can release it.
            return 0.15f;
        default:
            // Actions without cooldown return 0
            return 0.0f;
        }
    }

    void Humanizer::MarkActionUsed(RE::FormID a_formID, ActionType a_action)
    {
        float cooldown = GetCooldownForAction(a_action);
        if (cooldown <= 0.0f) {
            return; // No cooldown for this action
        }

        // Validate FormID before using it
        if (a_formID == RE::FormID(0)) {
            return; // Invalid FormID
        }

        // Strafe and forward dodge share the standard Dodge cooldown.
        ActionType cooldownKey = (a_action == ActionType::Strafe || a_action == ActionType::ForwardDodge)
                                     ? ActionType::Dodge
                                     : a_action;

        // Get-or-create the actor's cooldown state and record the cooldown atomically.
        m_cooldownStates.ModifyOrCreate(
            a_formID, [&](ActorCooldownState &cooldownState) { cooldownState.cooldowns.Emplace(cooldownKey, cooldown); });
    }

    void Humanizer::RecoverFromCorruption()
    {
        // Emergency recovery: clear all maps if they're corrupted
        // This is a last resort when maps become unusable
        // Thread-safe wrappers handle this safely
        m_reactionStates.Clear();
        m_cooldownStates.Clear();
    }

    void Humanizer::Update(float a_deltaTime)
    {
        // Update cooldowns and clean up expired ones
        // Use thread-safe iteration with write lock
        m_cooldownStates.WithWriteLock([&](auto &cooldownStatesMap) {
            auto actorIt = cooldownStatesMap.begin();
            while (actorIt != cooldownStatesMap.end()) {
                auto &cooldownState = actorIt->second;

                // Update nested cooldown map (also thread-safe)
                cooldownState.cooldowns.WithWriteLock([&](auto &cooldownsMap) {
                    auto it = cooldownsMap.begin();
                    while (it != cooldownsMap.end()) {
                        if (it->second > 0.0f) {
                            it->second -= a_deltaTime;
                            if (it->second <= 0.0f) {
                                // Cooldown expired, remove from map to save memory
                                it = cooldownsMap.erase(it);
                            } else {
                                ++it;
                            }
                        } else {
                            // Already zero or negative, remove it
                            it = cooldownsMap.erase(it);
                        }
                    }
                });

                // If all cooldowns expired, remove the entire actor entry
                cooldownState.cooldowns.WithReadLock([&](const auto &cooldownsMap) {
                    if (cooldownsMap.empty()) {
                        actorIt = cooldownStatesMap.erase(actorIt);
                    } else {
                        ++actorIt;
                    }
                });
            }
        });
    }

    void Humanizer::Cleanup()
    {
        // With FormID keys, cleanup is much simpler
        // FormIDs are stable identifiers - they don't become invalid
        // We rely on lazy cleanup: entries are removed when actors leave combat
        // (checked in CanReact() when actor is not in combat)
        // This avoids expensive LookupByID() calls that could crash

        // Note: We don't need to do anything here since:
        // 1. FormIDs don't become invalid (unlike pointers)
        // 2. Entries are cleaned up lazily in CanReact() when actors leave combat
        // 3. Expired cooldowns are cleaned up in Update()
        // 4. Avoiding LookupByID() prevents crashes from invalid FormIDs or deleted forms

        // If we want to be more aggressive, we could add a size limit and remove oldest entries
        // But for now, lazy cleanup is safer and more efficient
    }

    float Humanizer::GetMistakeMultiplierForAction(ActionType a_action) const
    {
        switch (a_action) {
        case ActionType::Bash:
            return m_config.bashMistakeMultiplier;
        case ActionType::Dodge:
            return m_config.dodgeMistakeMultiplier;
        case ActionType::Jump:
            return m_config.jumpMistakeMultiplier;
        case ActionType::Strafe:
            return m_config.strafeMistakeMultiplier;
        case ActionType::PowerAttack:
            return m_config.powerAttackMistakeMultiplier;
        case ActionType::Attack:
            return m_config.attackMistakeMultiplier;
        case ActionType::SprintAttack:
            return m_config.sprintAttackMistakeMultiplier;
        case ActionType::Retreat:
            return m_config.retreatMistakeMultiplier;
        case ActionType::Backoff:
            return m_config.backoffMistakeMultiplier;
        case ActionType::Advancing:
            return m_config.advancingMistakeMultiplier;
        case ActionType::Flanking:
            return m_config.flankingMistakeMultiplier;
        default:
            return 1.0f; // Default multiplier for unknown actions
        }
    }

    float Humanizer::CalculateMistakeChance(std::uint16_t a_level, ActionType a_action) const
    {
        // Calculate base mistake chance based on level
        // Calculate base mistake chance using linear reduction
        float reduction = static_cast<float>(a_level) * m_config.mistakeChanceReductionPerLevel;
        float baseMistakeChance = (std::max)(m_config.minMistakeChance, m_config.baseMistakeChance - reduction);

        // Apply action-specific multiplier
        float multiplier = GetMistakeMultiplierForAction(a_action);
        return baseMistakeChance * multiplier;
    }

    float Humanizer::CalculateReactionDelay(std::uint16_t a_level) const
    {
        // Calculate reaction delay using linear reduction
        float reduction = static_cast<float>(a_level) * m_config.reactionDelayReductionPerLevelMs;
        return (std::max)(m_config.minReactionDelayMs, m_config.baseReactionDelayMs - reduction);
    }

    void Humanizer::InitializeReactionDelay(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return;
        }

        // Get FormID safely - use FormID as key instead of raw pointer
        auto formIDOpt = ActorUtils::SafeGetFormID(a_actor);
        if (!formIDOpt.has_value()) {
            return; // Can't get FormID, actor is invalid
        }
        RE::FormID formID = formIDOpt.value();

        // Re-validate actor is still in combat before accessing
        if (!ActorUtils::SafeIsInCombat(a_actor)) {
            // Actor left combat, don't initialize
            return;
        }

        // Validate FormID before using it
        if (formID == RE::FormID(0)) {
            return; // Invalid FormID
        }

        // Calculate base delay based on actor level
        // Use wrapper to safely get level - validate actor again before accessing
        if (!ActorUtils::SafeIsInCombat(a_actor)) {
            // Actor left combat during initialization, clean up
            m_reactionStates.Erase(formID);
            return;
        }

        std::uint16_t level = ActorUtils::SafeGetLevel(a_actor);
        float baseDelay = CalculateReactionDelay(level);

        // Random variance: base + variance
        std::uniform_real_distribution<float> dist(0.0f, m_config.reactionVarianceMs);
        float variance = dist(g_gen);

        // Get-or-create the reaction state and initialize it atomically under the lock.
        m_reactionStates.ModifyOrCreate(formID, [&](ActorReactionState &state) {
            state.reactionDelay = baseDelay + variance;
            state.reactionTimer = 0.0f;
            state.canReact = false;
        });
    }
} // namespace CombatAI
