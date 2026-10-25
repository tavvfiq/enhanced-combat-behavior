#pragma once

#include "ActorStateData.h"
#include "pch.h"

namespace CombatAI
{
    // TK Dodge integration for NPC evasion
    class DodgeSystem
    {
      public:
        struct Config
        {
            float dodgeStaminaCost = 10.0f;
            float iFrameDuration = 0.3f;
            bool enableStepDodge = false;
            bool enableDodgeAttackCancel = true;
        };

        DodgeSystem() = default;
        ~DodgeSystem() = default;

        // Check if actor can dodge
        bool CanDodge(RE::Actor *a_actor);

        // Dodge along the first of a_options (horizontal directions, best first) whose path is clear: ground
        // within reach and no wall, object or actor in the way. False if none is.
        bool ExecuteDodge(RE::Actor *a_actor, std::initializer_list<RE::NiPoint3> a_options);

        // Execute dodge away from target (for evasion)
        bool ExecuteEvasionDodge(RE::Actor *a_actor, const ActorStateData &a_state);

        // Configuration
        void SetConfig(const Config &a_config) { m_config = a_config; }
        const Config &GetConfig() const { return m_config; }

      private:
        // Why a dodge along a_dir (horizontal unit vector) would fail ("ledge", "obstacle", "actor"), or nullptr
        const char *Blocked(RE::Actor *a_actor, const RE::NiPoint3 &a_dir);

        // Determine dodge direction based on target position
        std::string DetermineDodgeDirection(RE::Actor *a_actor, const RE::NiPoint3 &a_dodgeDirection);

        // Check if actor is currently dodging
        bool IsDodging(RE::Actor *a_actor);

        // Apply stamina cost for dodging (matches TK Dodge RE behavior)
        void ApplyDodgeStaminaCost(RE::Actor *a_actor);

        Config m_config;
    };
} // namespace CombatAI
