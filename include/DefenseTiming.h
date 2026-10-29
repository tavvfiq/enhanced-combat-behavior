#pragma once

#include "DecisionResult.h"
#include "pch.h"

// Defense timing log: how long before the blow a parry, timed block, defensive bash or dodge started, against the
// window it needs (parry/bash: ParryWindowStart..End, timed block: TimedBlockWindowStart..End, dodge: 0..IFrameDuration
// before the hit). Measures the brain's real timing (tick, tree re-read, reaction) before any timing model is tuned.
// A block or bash starts when the tree takes its intent (1.5.0 run: 0.21 s median after the brain's commit), a dodge at
// the commit.
// Per-commit lines for the selected NPC; a summary of every NPC once a minute.
namespace CombatAI::DefenseTiming
{
    // a_elapsed: seconds since the target's swing started, at commit; a_predicted: the expected seconds to the hit;
    // a_swing: the target's swing count (a second commit in one swing replaces the first); a_source: what a_predicted
    // came from (clip, median, none: a string literal, logged)
    void OnCommit(RE::Actor *a_self, RE::FormID a_target, ActionType a_action, float a_elapsed, float a_predicted,
                  std::uint32_t a_swing, const char *a_source);

    // The tree took a block or bash intent: the defense starts now
    void OnStart(RE::FormID a_self);

    // A melee blow from a_attacker landed on a_victim a_elapsed seconds into the swing (blocked or not)
    void OnHit(RE::Actor *a_victim, RE::FormID a_attacker, float a_elapsed, bool a_blocked);

    // Per frame: commits with no blow within 1.5 s resolve as "no hit"; the summary every 60 s
    void Update(float a_deltaTime);

    void Evict(RE::FormID a_formID);
} // namespace CombatAI::DefenseTiming
