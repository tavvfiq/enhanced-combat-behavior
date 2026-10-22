#pragma once

#include "pch.h"

namespace CombatAI
{
    // Action types that can be executed
    enum class ActionType : std::uint8_t
    {
        None = 0,
        Retreat,      // 1
        Strafe,       // 2
        Bash,         // 3
        PowerAttack,  // 4
        SprintAttack, // 5
        Attack,       // 6
        Jump,         // 7
        Dodge,        // 8
        Backoff,      // 9
        Advancing,    // 10
        Feint,        // 11 - Fake attack to bait enemy response
        Flanking,     // 12 - Tactical positioning with allies (pincer movement)
        Parry,        // 13 - Timed bash for parrying incoming attacks
        TimedBlock,   // 14 - Timed block for blocking incoming attacks
        ForwardDodge  // 15 - Directional gap-close dodge during advancing
    };

    inline const char *ActionName(ActionType a_action)
    {
        static constexpr const char *names[] = {"None",     "Retreat",  "Strafe",   "Bash",     "PowerAttack",
                                                "SprintAttack", "Attack", "Jump",   "Dodge",    "Backoff",
                                                "Advancing", "Feint",   "Flanking", "Parry",    "TimedBlock",
                                                "ForwardDodge"};
        const auto i = static_cast<std::size_t>(a_action);
        return i < std::size(names) ? names[i] : "?";
    }

    // Decision result containing action to take
    struct DecisionResult
    {
        ActionType action = ActionType::None;
        float priority = 0.0f;                                   // Higher = more important (float for granular control)
        RE::NiPoint3 direction = RE::NiPoint3(0.0f, 0.0f, 0.0f); // For movement actions
        float intensity = 1.0f;                                  // 0.0 to 1.0, for movement speed/intensity
        bool sprint = false; // Advancing: sprint toward the target (sprint-attack run-up)
        float holdDistance = 0.0f; // Strafe/Flanking: circle no closer than this (pacing wait ring)
        bool reflex = false;       // Projectile sidestep: has its own roll, skips the skill roll
        bool paced = false;        // A pacing wait ("waiting its turn"), not the brain's own choice
    };
} // namespace CombatAI
