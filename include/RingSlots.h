#pragma once

#include "DecisionResult.h"

namespace CombatAI
{
    struct ActorStateData;
}

// Ring slots (1.12.0). The melee NPCs fighting one target used to approach and wait in the same wedge in front of it
// (1.11.1 run: four bandits mostly within 60-90 degrees of each other, 1-6 crowded pairs per sample), and vanilla's
// Circle refused 15 of 21 starts (view cone, spots already reserved). Each target's melee attackers now get their own
// sector around it, spaced evenly and kept in their order. Sectors stay where they were given (1.12.1): a newcomer takes
// the middle of the biggest gap, the group evens out only when the gaps get lopsided, and an NPC far from its sector
// trades with a closer one instead of walking half a lap. An NPC approaching, or waiting its turn, outside its sector
// walks there through an arc around the
// target: an Advance whose goal is a point on that arc (the Advance path hook, NavmeshRedirect), so the game still
// paths, animates and avoids. Inside its sector it closes in (approach) or keeps circling (waiting) as before.
namespace CombatAI::RingSlots
{
    // Brain tick: a_actor (melee, brain-driven) fights a_target; keeps the target's roster and solves its sectors
    void Note(RE::Actor *a_actor, RE::Actor *a_target);

    // Before a decision executes: an approach (Advancing) or a pacing wait (held Flanking) outside the NPC's sector
    // becomes an Advance toward its sector, through an arc point (the goal the Advance path takes, see Goal)
    void Steer(RE::Actor *a_actor, DecisionResult &a_decision, const ActorStateData &a_state);

    // The ring goal for a_actor's Advance path, if one was set in the last half second (combat AI threads)
    bool Goal(RE::Actor *a_actor, RE::NiPoint3 &a_point);

    // Forget a_actor (death, combat end)
    void Evict(RE::FormID a_formID);
} // namespace CombatAI::RingSlots
