#pragma once

#include "pch.h"

// Clip timing probe (measurement only): reads the attacker's playing attack clip (the one OAR chose: the graph's active
// hkbClipGenerator with a HitFrame annotation), its local time and measured playback rate, and predicts when the
// blow lands. When the blow lands, that prediction and the brain's current one (learned median swing time) are both
// compared with the real moment. Decides whether clip timing should replace the median (moveset proposal, step 1).
namespace CombatAI::ClipTiming
{
    // A brain tick of an NPC whose target is swinging: sample the attacker's clip (at most once per 30 ms per
    // attacker). a_medianTimeToHit: the brain's prediction this tick (999 = none); a_debug: the NPC is selected;
    // a_swing: that NPC's count of the attacker's swings (the log's swing tag).
    void Sample(RE::Actor *a_attacker, float a_medianTimeToHit, bool a_debug, std::uint32_t a_swing);

    // What the attacker's clip says about its next blow
    enum class Eta
    {
        kOff,    // clip timing is off (UsePlayerClipTiming); Predict never returns it
        kNoClip, // no attack clip read in the last 0.15 s
        kHeld,   // the clip is held (BFCO deciding light or power): no timing yet
        kStale,  // the clip read is the previous swing's, its blows past (the new swing's clip not read yet)
        kBlow,   // a blow ahead: a_earliest/a_latest set
        kPast    // the clip's blows are all past (it missed or already hit): none coming, both 999
    };
    const char *Name(Eta a_eta);

    // Time to the attacker's next blow from its clip, as read by the last Sample (at most 0.15 s ago). a_latest: the
    // HitFrame plus the measured lag of the hit event; a_earliest: when weapon contact can first land (Precision's hit
    // window opening, else the HitFrame). kNoClip/kHeld/kStale: callers keep the median rule. a_swingElapsed: seconds
    // since the caller saw this swing start (< 0: unknown)
    Eta Predict(RE::Actor *a_attacker, float a_swingElapsed, float &a_earliest, float &a_latest);

    // A melee blow from a_attacker landed on a_victim; a_swing: the victim's count of the attacker's swings
    void OnHit(RE::Actor *a_attacker, RE::Actor *a_victim, bool a_power, std::uint32_t a_swing);

    // Debug, the selected NPC's own attacks (each brain tick, main thread): each attack clip it starts is logged once
    // as an attack profile read from its annotations (startup, blows, motion by the first blow, recovery, combo window,
    // next slots) with whether that blow reaches its target from here: a_distance to the target, a_reach its weapon
    // reach, a_targetRadius the target's. Moveset proposal, build step 2; nothing is decided from it yet
    void LogOwnAttack(RE::Actor *a_actor, float a_distance, float a_reach, float a_targetRadius);

    // Per frame: the 60 s summary
    void Update(float a_deltaTime);

    void Evict(RE::FormID a_formID);
} // namespace CombatAI::ClipTiming
