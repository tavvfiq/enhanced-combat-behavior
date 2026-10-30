#pragma once

// Advance goal on the navmesh (1.10.0). An Advance paths to the target's own position; when that spot has no navmesh
// triangle under it (a hole, or ground more than 64 units below the navmesh: the game's default lookup limit), the
// path search finds no route and the Advance fails every ~1 s while the NPC stands still (1.9.4 run: 30 s, the
// goal's triangle 65535 on every try). After an Advance failed, the next one aims at the closest navmesh point next to
// the target instead; the game still does the pathing, animation and avoidance.
namespace CombatAI::NavmeshRedirect
{
    // Hook the Advance path's request fill (vtable slot 7 of its CombatPath class). Call with the other hooks.
    void Install();

    // An Advance node of a_actor failed (combat AI threads): its next Advances may be redirected for a few seconds
    void NoteAdvanceFailed(RE::Actor *a_actor);

    // Forget a_actor (death, combat end)
    void Evict(RE::FormID a_formID);
} // namespace CombatAI::NavmeshRedirect
