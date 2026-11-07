#pragma once

#include "pch.h"

// Guns (1.16.0 targets, 1.17.0 NPC gunmen). Pistol mods make their guns one-handed axes (One-Handed Crossbow
// Framework) or staves, so the engine and the brain read them as melee weapons. [DecisionMatrix] GunKeywords lists
// keyword EditorIDs that mark a gun whatever its weapon type; keywords keep their EditorIDs in memory, KID's too.
namespace CombatAI::Guns
{
    // a_item is a weapon carrying one of the GunKeywords
    bool IsGun(RE::TESForm *a_item);

    // What an actor holds: a gun in either hand, and whether the other hand holds a melee weapon
    struct Hands
    {
        bool right = false;
        bool left = false;
        bool meleeOther = false; // a sword, axe, mace or dagger in the other hand (not a bow, crossbow, staff or spell)
        bool Any() const { return right || left; }
    };
    Hands Of(RE::Actor *a_actor);

    // Fights with guns only (one, two, or with a spell or staff): a ranged fighter, never a melee one
    bool IsGunman(RE::Actor *a_actor);

    // A gunman's firing range from its personality: careful ones keep far, reckless ones close
    struct Band
    {
        float backOff = 350.0f; // inside this it backs away
        float ideal = 650.0f;   // where it holds
        float max = 950.0f;     // beyond this it moves in
    };
    Band RangeOf(RE::Actor *a_actor);
} // namespace CombatAI::Guns
