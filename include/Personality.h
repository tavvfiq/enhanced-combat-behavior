#pragma once

#include "pch.h"

// NPC personality from data every NPC already has: the Confidence (0 Cowardly .. 4 Foolhardy) and Aggression
// (0 Unaggressive .. 3 Frenzied) actor values, the combat style's offensive / defensive multipliers and its
// avoid-threat chance, plus a small fixed per-NPC jitter (from the FormID) so two bandits of one template differ.
// Three numbers in 0..1 scale the brain's risky and careful rolls instead of one chance for everyone.
namespace CombatAI::Personality
{
    struct Traits
    {
        float risk = 0.45f;     // forward dodge, sprint attack, trading blows
        float caution = 0.45f;  // back off, retreat, block, dodge (used from 0.27)
        float patience = 0.45f; // feints, waiting for openings (used from 0.27)
    };

    // Cached per actor for 10 s (actor values can change: frenzy, rally, fear)
    Traits Get(RE::Actor *a_actor);

    // Multiplier for a risky chance: 0.25 (risk 0) .. 1.75 (risk 1); about 0.9 for an average bandit
    constexpr float RiskScale(float a_risk) { return 0.25f + 1.5f * a_risk; }
    static_assert(RiskScale(0.0f) == 0.25f && RiskScale(1.0f) == 1.75f);
} // namespace CombatAI::Personality
