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

    // Probe of the attack, bash and block animation events sent to the console-selected NPC and whether its
    // graph accepted them (log only). For every NPC, an accepted attack/bash start event also starts the
    // brain's anti-spam action timer (CombatDirector::NotifySwingStarted).
    // Hooks Character's NotifyAnimationGraph (IAnimationGraphManagerHolder, VTABLE_Character[3] slot 1).
    // Also logs the hit-cone and range/path checks vanilla StartAttack runs before it fires an attack or bash.
    void Install();
} // namespace CombatAI::EngineProbes
