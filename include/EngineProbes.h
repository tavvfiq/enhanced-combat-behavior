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

    // Log-only probe of the attack, bash and block animation events sent to the console-selected NPC and
    // whether its graph accepted them: shows where an attack intent the tree took gets lost.
    // Hooks Character's NotifyAnimationGraph (IAnimationGraphManagerHolder, VTABLE_Character[3] slot 1).
    void Install();
} // namespace CombatAI::EngineProbes
