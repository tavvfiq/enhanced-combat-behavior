#pragma once

#include "pch.h"

namespace CombatAI::EngineProbes
{
    // Log-only probe of the engine's combat threat list (CombatManager+0x28): projectiles,
    // runes and explosions the AI tracks. Logs only with EnableDebugLog on, for the
    // console-selected actor, and changes no behavior.
    // (An AnticipateAttackHandler probe showed it fires only for NPC attacks, at the wind-up
    // frame attack-state polling already sees, and never for the player's: removed.)
    // Call once per frame (CombatDirector::Update)
    void Update(float a_deltaTime);
} // namespace CombatAI::EngineProbes
