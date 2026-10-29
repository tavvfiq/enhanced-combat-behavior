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

        // Which roll an action takes: one cached result per class per opportunity
        enum class SkillClass
        {
            None,    // movement and the like: never rolls
            Defense, // reacting to the target: dodge, parry, timed block, bash, jump
            Offense  // committing to an attack or feint
        };
        static SkillClass ClassOf(ActionType a_action);

        Humanizer() = default;
        ~Humanizer() = default;

        // Combat-entry reaction delay: false until the actor has been in combat for one rolled delay
        bool CanReact(RE::Actor *a_actor, RE::FormID a_formID);

        // Skill roll for a deliberate action, cached per opportunity. True = go ahead
        // (also for actions that never roll).
        bool PassesSkillCheck(RE::Actor *a_actor, RE::FormID a_formID, ActionType a_action,
                              const ActorStateData &a_state, float a_combatTime);

        // What a skill chance is made of: level skill, the actor-value gap to the target, then fatigue and panic
        struct SkillParts
        {
            float level = 0.0f;       // SkillBase + SkillPerLevel * (level - 1), clamped
            float selfValue = 0.0f;   // the NPC's actor value for this action (0 = none)
            float targetValue = 0.0f; // the target's opposing actor value (0 = none)
            float gapSkill = 0.0f;    // skill added by the gap (SkillActorValueWeight)
            float fatigue = 0.0f;     // 0..1
            float panic = 0.0f;       // 0..1
            float chance = 1.0f;      // the pass chance
        };
        SkillParts ExplainSkill(RE::Actor *a_actor, ActionType a_action, float a_combatTime,
                                const ActorStateData &a_state) const;

        // Skill chance for this actor right now (level, actor-value gap, fatigue, panic; a boss panics only when
        // desperate)
        float SkillChance(RE::Actor *a_actor, ActionType a_action, float a_combatTime, const ActorStateData &a_state) const
        {
            return ExplainSkill(a_actor, a_action, a_combatTime, a_state).chance;
        }

        // Exertion: stamina spent, decaying over FatigueExertionSeconds (call once per brain tick)
        void TrackExertion(RE::FormID a_formID, float a_stamina, float a_maxStamina, float a_elapsed);

        // One roll per opportunity a_key (e.g. one swing): the same key returns the stored result. Backoff only.
        bool RollBackoff(RE::FormID a_formID, std::uint64_t a_key, float a_chance);
        float Now() const { return m_time.load(std::memory_order_relaxed); }

        // Check if action is on cooldown
        bool IsOnCooldown(RE::FormID a_formID, ActionType a_action);

        // Mark action as used (start cooldown)
        void MarkActionUsed(RE::FormID a_formID, ActionType a_action);

        // Update internal state (call per frame)
        void Update(float a_deltaTime);

        // Cleanup invalid actors (call periodically)
        void Cleanup();

        void Evict(RE::FormID a_formID)
        {
            m_skillRolls.Erase(a_formID);
            m_exertion.Erase(a_formID);
        }

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
            std::uint64_t backoffKey = 0;
            bool backoffPassed = false;
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
        struct Exertion
        {
            float last = -1.0f; // stamina at the last tick (-1: none yet)
            float spent = 0.0f; // stamina spent, decaying
            float fraction = 0.0f; // spent / max stamina, 0..1
        };
        ThreadSafeMap<RE::FormID, Exertion> m_exertion;
        ThreadSafeMap<RE::FormID, ActorCooldownState> m_cooldownStates;

        // Fatigue (exertion or combat time, and low stamina) and panic (low health, if a_panics), each 0..1
        std::pair<float, float> CalculateStress(RE::FormID a_formID, ActionType a_action, float a_combatTime,
                                                const SelfState &a_self, bool a_panics) const;

        // Level-scaled delay plus random variance, in seconds
        float RollReactionDelaySeconds(RE::Actor *a_actor) const;

        // Calculate reaction delay based on actor level
        float CalculateReactionDelay(std::uint16_t a_level) const;

        // Get cooldown duration for an action type
        float GetCooldownForAction(ActionType a_action) const;
    };
} // namespace CombatAI
