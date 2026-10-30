#pragma once

// Flanking combat styles (1.11.0). Vanilla moves an NPC whose combat style has the flanking flag through its own
// FlankingMovement tree (stalk, flank in 96-384 unit steps, flank from up to 1024 units out, watch): it ignores turn
// taking and openings and mostly hangs far back (1.10.2 run: a Marauder Rogue at 300-450 units, out to 972). For NPCs
// the brain drives, the Movement selector's flanking check answers "no", so they take the brain's CloseMovement, and
// the brain plays the style: while waiting its turn it circles toward the target's back, and it presses attacks from
// the side or back and on a target busy with someone else.
namespace CombatAI::FlankingStyle
{
    // Patch the Movement selector's flanking check (SE 48462 / AE 49453). Call with the other hooks.
    void Install(SKSE::Trampoline &a_trampoline);

    // a_actor's combat style has the flanking flag
    bool Is(RE::Actor *a_actor);
} // namespace CombatAI::FlankingStyle
