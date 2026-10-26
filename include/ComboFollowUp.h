#pragma once

#include "ActorStateData.h"
#include "DecisionResult.h"
#include "pch.h"

// Our own combo follow-up for MCO / BFCO movesets (replaces SCAR's combo driving; off while SCAR.dll is loaded).
// The movesets mark when the next attack may start with annotation events: MCO_WinOpen / MCO_WinClose (normal),
// MCO_PowerWinOpen / MCO_PowerWinClose (power), BFCO_NextWinStart / BFCO_NextPowerWinStart / BFCO_DIY_EndLoop.
// The behavior graph already knows which attack comes next (MCO_nextattack, BFCO_NextIsAttackN); inside an open
// window we only start "the next attack": the moveset's NPC attack idle (MCO's ADXP_NPCNormalAttack /
// ADXP_NPCPowerAttack) through TESActionData, as SCAR does (SCAR 2.0.1 PerformSpecialIdle, MIT, max-su-2019).
namespace CombatAI::ComboFollowUp
{
    // Hooks the Character anim event sink (VTABLE_Character[2] slot 1) to track the windows. Not when SCAR.dll is loaded.
    void Install();

    // Brain tick: inside an open window, roll once per window to start the next attack (power first if its window
    // is open and stamina allows). Not while waiting for an attack slot, defending, or with the target out of reach.
    void Update(RE::Actor *a_actor, const ActorStateData &a_state, const DecisionResult &a_decision);
} // namespace CombatAI::ComboFollowUp
