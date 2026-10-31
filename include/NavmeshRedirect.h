#pragma once

// Advance goal on the navmesh (1.10.0). An Advance paths to the target's own position; when that spot has no navmesh
// triangle under it (a hole, or ground more than 64 units below the navmesh: the game's default lookup limit), the
// path search finds no route and the Advance fails every ~1 s while the NPC stands still (1.9.4 run: 30 s, the
// goal's triangle 65535 on every try). After an Advance failed, the next one aims at the closest navmesh point next to
// the target instead; the game still does the pathing, animation and avoidance. Flanking NPCs (combat styles with
// flanking) path through Flank and Flank distant paths, whose goal can sit off the navmesh too (1.10.1 run: a Marauder
// Rogue's distant flank goal had no navmesh): those goals are moved the same way, without waiting for a failure.
// Some navmesh is walkable but cut off (1.13.3 run: one triangle on a hill in every instant "no path" of three runs,
// our Advance and vanilla's alike). An Advance that got no path is remembered by its goal for 15 s, for every NPC; a
// later Advance goal near one moves toward the NPC onto other navmesh (1.13.4). An Advance that walks toward a goal
// near a remembered one clears it (1.13.5: the target moves, and the memory had covered the whole fight).
namespace CombatAI::NavmeshRedirect
{
    // Hook the request fill of the Advance, Flank and Flank distant paths (vtable slot 7 of their CombatPath classes)
    void Install();

    // An Advance node of a_actor failed (combat AI threads): its next Advances may be redirected for a few seconds
    void NoteAdvanceFailed(RE::Actor *a_actor);

    // a_actor's Advance failed without moving: its path request's goal (a navmesh point) got no path
    void NoteNoPath(RE::Actor *a_actor, const RE::NiPoint3 &a_goal);

    // a_actor's Advance walked (>= 40 units) toward a_goal: remembered no-path goals near it are cleared
    void NotePathWalked(RE::Actor *a_actor, const RE::NiPoint3 &a_goal);

    // Forget a_actor (death, combat end)
    void Evict(RE::FormID a_formID);
} // namespace CombatAI::NavmeshRedirect
