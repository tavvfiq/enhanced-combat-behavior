#pragma once

#include "pch.h"

namespace CombatAI
{
    struct ActorStateData;
}

// Attack Warp integration (1.15.0, optional: off when AttackWarp.dll is absent or reports no V1 API). Attack Warp turns
// and stretches melee lunges toward the target; ECB never turns or moves an attacker itself. With it loaded ECB:
//   - sets each NPC's tracking from what the brain already knows (SetTuning): turn rate and aim lead from the NPC's
//     skill roll, lunge stretch from its personality risk, more for an enraged or all-in boss; cleared when it leaves;
//   - turns stretching off while an NPC waits its turn: movesets (BFCO, MCO) pick their own attacks, only distance
//     keeps a waiter's swing from landing, and a stretched lunge reaches from the waiting ring;
//   - reads every swing back (GetSwingInfo) and logs it: per swing for the selected NPC (aim error at the lock and at
//     the end, stretch, predicted time to hit, hit or miss), the player's swing phases (whether they follow True
//     Directional Movement's steering), and a 60 s summary with the hits and health the player lost.
// Nothing here changes a decision yet: the logs decide the next steps (dodge timing from timeToHit, a learned reach).
namespace CombatAI::AttackWarp
{
    // kDataLoaded: request the API (logs found or missing)
    void Initialize();

    bool Available();

    // Brain tick, after pacing: a_skill is the NPC's attack skill roll, a_held whether it waits its turn
    void Observe(RE::Actor *a_actor, const ActorStateData &a_state, float a_skill, bool a_held);

    // Every frame: the player's swing and health, the 60 s summary
    void Tick(float a_deltaTime);

    // A melee hit landed (any attacker, any victim)
    void NoteHit(RE::Actor *a_attacker, RE::Actor *a_victim, bool a_blocked);

    // a_formID died or left combat: its tuning is cleared
    void Evict(RE::FormID a_formID);
} // namespace CombatAI::AttackWarp
