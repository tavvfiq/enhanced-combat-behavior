#pragma once

#include "ActorStateData.h"
#include "DecisionResult.h"
#include "pch.h"

namespace CombatAI
{
    // Enhances combat style behavior instead of suppressing vanilla AI
    class CombatStyleEnhancer
    {
      public:
        CombatStyleEnhancer() = default;
        ~CombatStyleEnhancer() = default;

        // Enhance decision based on combat style
        // Modifies decision priority, action type, and parameters based on NPC's combat style
        DecisionResult EnhanceDecision(RE::Actor *a_actor, const DecisionResult &a_baseDecision,
                                       const ActorStateData &a_state);

        // Get combat style for actor
        static RE::TESCombatStyle *GetCombatStyle(RE::Actor *a_actor);

      private:
        // Helper: Check if actor has melee weapon equipped
        bool HasMeleeWeapon(RE::Actor *a_actor) const;

        // Helper: Check if actor has ranged weapon equipped
        bool HasRangedWeapon(RE::Actor *a_actor) const;

        // Helper: Check if actor has magic equipped
        bool HasMagicEquipped(RE::Actor *a_actor) const;

        // Enhance based on combat style flags and multipliers
        DecisionResult EnhanceForDuelingStyle(RE::Actor *a_actor, const DecisionResult &a_decision,
                                              const ActorStateData &a_state, RE::TESCombatStyle *a_style);
        DecisionResult EnhanceForFlankingStyle(RE::Actor *a_actor, const DecisionResult &a_decision,
                                               const ActorStateData &a_state, RE::TESCombatStyle *a_style);
        DecisionResult EnhanceForAggressiveStyle(RE::Actor *a_actor, const DecisionResult &a_decision,
                                                 const ActorStateData &a_state, RE::TESCombatStyle *a_style);
        DecisionResult EnhanceForDefensiveStyle(RE::Actor *a_actor, const DecisionResult &a_decision,
                                                const ActorStateData &a_state, RE::TESCombatStyle *a_style);
        DecisionResult EnhanceForMagicStyle(RE::Actor *a_actor, const DecisionResult &a_decision,
                                            const ActorStateData &a_state, RE::TESCombatStyle *a_style);
        DecisionResult EnhanceForRangedStyle(RE::Actor *a_actor, const DecisionResult &a_decision,
                                             const ActorStateData &a_state, RE::TESCombatStyle *a_style);

        // Shift evasion vs. guard priorities by body armor weight (independent of combat style)
        static void ApplyArmorStyle(DecisionResult &a_decision, const ActorStateData &a_state);

        // Apply combat style multipliers
        void ApplyStyleMultipliers(RE::TESCombatStyle *a_style, DecisionResult &a_decision,
                                   const ActorStateData &a_state);

        // Helper: Calculate strafe direction
        RE::NiPoint3 CalculateStrafeDirection(const ActorStateData &a_state);
    };
} // namespace CombatAI
