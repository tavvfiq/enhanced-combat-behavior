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

    // Duelists (1.19.0): a gun and a melee weapon. The gun framework fires an NPC's gun on its own timer (not attacking,
    // blocking or sprinting, line of sight), so from range a duelist holds still for that shot, then closes in.
    // NoteShot: the framework released a_actor's aimed gun (its MLh/MRh_SpellRelease_Event)
    void NoteShot(RE::Actor *a_actor);
    // Call each brain tick while in pistol range: whether it still holds for its shot. The hold ends once its gun fires
    // or after a_maxSeconds, and none starts within a_maxSeconds of a shot.
    struct Hold
    {
        bool holding = false;
        bool started = false;      // this call began the hold
        float held = 0.0f;         // seconds held so far
        const char *why = nullptr; // set when this call ended the hold (or skipped it)
    };
    Hold DuelHold(RE::FormID a_formID, float a_maxSeconds);
    // Back in melee range: the next time out in pistol range it holds again
    void EndDuelHold(RE::FormID a_formID);
    void Evict(RE::FormID a_formID);
} // namespace CombatAI::Guns
