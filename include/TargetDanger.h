#pragma once

#include "pch.h"

// Target danger: how much an NPC should fear what it is fighting, 0 (nothing special) .. 1, rated against its current
// target and refreshed every 5 s or on a target change. Parts, combined as independent risks (1 - product of 1 - x):
// - strength: the target's level or max health well above the NPC's own (TargetDangerStrengthWeight);
// - huge: giants, mammoths, dwarven centurions, lurkers (race FormIDs, Skyrim.esm / Dragonborn.esm) and dragons
//   (ActorTypeDragon keyword): 0.6;
// - beast: werewolves, werebears, vampire lords (race FormIDs, Skyrim.esm / Dragonborn.esm / Dawnguard.esm): 0.5;
// - a boss (Boss.h): 0.3.
// What it changes: risk down and caution up in Personality::Effective (not for bosses, not for Foolhardy NPCs), the
// threat level up (0.4 Moderate, 0.7 High), no bash / parry / timed block against a huge target's swings, no running
// from a beast (it is faster; a drink back-off still goes), and extra attack slots against a huge or beast target.
namespace CombatAI
{
    struct ActorStateData;
}

namespace CombatAI::TargetDanger
{
    struct Danger
    {
        float score = 0.0f;
        bool huge = false;
        bool beast = false;
    };

    // Brain tick for an NPC (after GatherState): rates its target and raises a_state's threat level to match
    void Update(RE::Actor *a_actor, ActorStateData &a_state, bool a_debug);

    // The NPC's last rating (zero when off, no target, or not rated yet)
    Danger Of(RE::Actor *a_actor);

    void Evict(RE::FormID a_formID);
} // namespace CombatAI::TargetDanger
