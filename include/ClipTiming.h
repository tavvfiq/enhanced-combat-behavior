#pragma once

#include "pch.h"

// Clip timing probe (measurement only): reads the attacker's playing attack clip (the one OAR chose: the graph's active
// hkbClipGenerator with a HitFrame annotation), its local time and measured playback rate, and predicts when the
// blow lands. When the blow lands, that prediction and the brain's current one (learned median swing time) are both
// compared with the real moment. Decides whether clip timing should replace the median (moveset proposal, step 1).
namespace CombatAI::ClipTiming
{
    // A brain tick of an NPC whose target is swinging: sample the attacker's clip (at most once per 30 ms per
    // attacker). a_medianTimeToHit: the brain's prediction this tick (999 = none); a_debug: the NPC is selected.
    void Sample(RE::Actor *a_attacker, float a_medianTimeToHit, bool a_debug);

    // Time to the attacker's next blow from its clip, as read by the last Sample (at most 0.15 s ago). a_latest: the
    // HitFrame plus the measured lag of the hit event; a_earliest: when weapon contact can first land (Precision's hit
    // window opening, else the HitFrame). False: no attack clip, rate not measured yet, or no blow ahead (callers keep
    // the median rule)
    bool Predict(RE::Actor *a_attacker, float &a_earliest, float &a_latest);

    // A melee blow from a_attacker landed on a_victim
    void OnHit(RE::Actor *a_attacker, RE::Actor *a_victim, bool a_power);

    // Per frame: the 60 s summary
    void Update(float a_deltaTime);

    void Evict(RE::FormID a_formID);
} // namespace CombatAI::ClipTiming
