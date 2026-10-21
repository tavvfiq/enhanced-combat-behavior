#pragma once

#include "pch.h"

namespace CombatAI
{
    // Shapes the vanilla melee attack node's per-attack chance (hook in Hooks.cpp).
    // The node weights its candidate attacks by this chance, then rolls the picked one
    // against it: 0 vetoes the attack, larger values make it likelier (>= 1 always passes).
    // The engine still picks and plays the moveset's own attack data.
    class VanillaAttackShaper
    {
      public:
        static VanillaAttackShaper &GetInstance()
        {
            static VanillaAttackShaper instance;
            return instance;
        }

        VanillaAttackShaper(const VanillaAttackShaper &) = delete;
        VanillaAttackShaper &operator=(const VanillaAttackShaper &) = delete;

        // Called from AI threads with the engine's chance for one candidate attack
        float Adjust(RE::Actor *a_attacker, RE::Actor *a_target, RE::BGSAttackData *a_data, float a_chance);

      private:
        VanillaAttackShaper() = default;
        ~VanillaAttackShaper() = default;
    };
} // namespace CombatAI
