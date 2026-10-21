#pragma once

#include "ActorStateData.h"
#include "Config.h"
#include "DecisionResult.h"
#include "ThreadSafeMap.h"
#include "pch.h"
#include <atomic>

namespace CombatAI
{
    // Organic feel: a one-time combat-entry reaction delay, skill rolls and cooldowns.
    //
    // Skill rolls replace per-tick "mistakes" and the delay on every action change (both
    // mostly added stutter and ate timing windows). A deliberate action (dodge, parry, timed
    // block, bash, jump, attacks, feint) rolls once per opportunity - one incoming swing, or
    // one offense window - and a failed roll lets that opportunity pass: a real mistake.
    // Movement never rolls.
    class Humanizer
    {
      public:
        using Settings = Config::HumanizerSettings;

        Humanizer() = default;
        ~Humanizer() = default;

        // Combat-entry reaction delay: false until the actor has been in combat for one rolled delay
        bool CanReact(RE::Actor *a_actor, RE::FormID a_formID);

        // Skill roll for a deliberate action, cached per opportunity. True = go ahead
        // (also for actions that never roll).
        bool PassesSkillCheck(RE::Actor *a_actor, RE::FormID a_formID, ActionType a_action,
                              const ActorStateData &a_state, float a_combatTime);

        // Skill chance for this actor right now (level, fatigue, panic)
        float SkillChance(RE::Actor *a_actor, ActionType a_action, float a_combatTime, const SelfState &a_self) const;

        // Check if action is on cooldown
        bool IsOnCooldown(RE::FormID a_formID, ActionType a_action);

        // Mark action as used (start cooldown)
        void MarkActionUsed(RE::FormID a_formID, ActionType a_action);

        // Update internal state (call per frame)
        void Update(float a_deltaTime);

        // Cleanup invalid actors (call periodically)
        void Cleanup();

        // Emergency recovery: clear corrupted maps if they become unusable
        void RecoverFromCorruption();

        void Evict(RE::FormID a_formID) { m_skillRolls.Erase(a_formID); }

        // Configuration
        void SetConfig(const Settings &a_config) { m_config = a_config; }
        const Settings &GetConfig() const { return m_config; }

      private:
        // Times are seconds on m_time (advanced in Update)
        struct ActorReactionState
        {
            float entryReadyTime = -1.0f; // Combat-entry delay ends here (-1 = not rolled yet)
        };

        // Last roll per action class, valid while the opportunity key matches
        struct SkillState
        {
            std::uint64_t defenseKey = 0;
            bool defensePassed = false;
            std::uint64_t offenseKey = 0;
            bool offensePassed = false;
        };

        std::atomic<float> m_time{0.0f};

        struct ActorCooldownState
        {
            ThreadSafeMap<ActionType, float> cooldowns;

            ActorCooldownState() = default;
            ActorCooldownState(ActorCooldownState &&) = default;
            ActorCooldownState &operator=(ActorCooldownState &&) = default;
            ActorCooldownState(const ActorCooldownState &) = delete;
            ActorCooldownState &operator=(const ActorCooldownState &) = delete;
        };

        Settings m_config;

        ThreadSafeMap<RE::FormID, ActorReactionState> m_reactionStates;
        ThreadSafeMap<RE::FormID, SkillState> m_skillRolls;
        ThreadSafeMap<RE::FormID, ActorCooldownState> m_cooldownStates;

        // Extra skill penalty from fatigue (combat time, low stamina) and panic (low health)
        float CalculateStress(ActionType a_action, float a_combatTime, const SelfState &a_self) const;

        // Level-scaled delay plus random variance, in seconds
        float RollReactionDelaySeconds(RE::Actor *a_actor) const;

        // Calculate reaction delay based on actor level
        float CalculateReactionDelay(std::uint16_t a_level) const;

        // Get cooldown duration for an action type
        float GetCooldownForAction(ActionType a_action) const;
    };
} // namespace CombatAI
