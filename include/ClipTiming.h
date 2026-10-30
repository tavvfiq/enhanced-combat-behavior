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

    // An NPC's own attacks (each brain tick, main thread): each attack clip it starts is read once as an attack
    // profile from its annotations (startup, blows, motion by the first blow, recovery, combo window, next slots) and
    // kept per NPC (FastestAttack). a_log (the selected NPC): logged, with whether that blow reaches its target from
    // here: a_distance to the target, a_reach its weapon reach, a_targetRadius the target's; a_target's movement
    // toward or away from it over the clip's startup gives the distance at the blow too
    void ObserveOwnAttack(RE::Actor *a_actor, RE::Actor *a_target, float a_distance, float a_reach, float a_targetRadius,
                          bool a_log);

    // The fastest of a_actor's attacks seen so far (ObserveOwnAttack) whose first blow reaches a target a_distance
    // away (a_reach + a_targetRadius + the clip's forward motion): a_startup = seconds to its blow. 1 found, 0 attacks
    // known but none reaches, -1 none known
    int FastestAttack(RE::FormID a_actor, float a_distance, float a_reach, float a_targetRadius, float &a_startup);

    // How long a_attacker (the player: its clip is sampled) stays in its swing: a_recoverIn seconds to its clip's
    // MCO_Recovery mark (else the clip's end), a_windowIn seconds to its combo window (negative: open since; false
    // a_hasWindow: no window mark). False: no clip at swing speed read in the last 0.15 s
    bool Recovery(RE::Actor *a_attacker, float &a_recoverIn, float &a_windowIn, bool &a_hasWindow);

    // Debug, the selected NPC a_npc (main thread): its target a_attacker's swing a_swing is past its blows. Once per
    // swing: how long a_attacker stays committed (its clip's MCO_Recovery, and its combo window) against the NPC's
    // fastest attack seen so far (LogOwnAttack), in time and reach. Moveset proposal, punish; nothing decided yet
    void LogPunishWindow(RE::Actor *a_npc, RE::Actor *a_attacker, float a_distance, float a_reach, float a_targetRadius,
                         std::uint32_t a_swing);

    // Per frame: the 60 s summary
    void Update(float a_deltaTime);

    void Evict(RE::FormID a_formID);
} // namespace CombatAI::ClipTiming
