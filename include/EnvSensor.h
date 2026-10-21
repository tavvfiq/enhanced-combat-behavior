#pragma once

#include "pch.h"

namespace CombatAI
{
    // Environmental sensing via Havok raycasts. Used to give combat NPCs spatial
    // awareness the vanilla AI lacks: line-of-sight to their target (so they don't
    // swing/shoot through walls) and ledge detection (so they don't dodge off a
    // cliff). All casts run on the game thread from the Actor::Update hook, under a
    // read lock on the physics world, and fail-open so a sensing error never freezes
    // an actor's behaviour.
    namespace EnvSensor
    {
        // Result of a single ray cast.
        struct RayHit
        {
            bool hit = false;       // something was struck before the ray end
            float fraction = 1.0f;  // 0..1 position of the hit along the ray
            RE::TESObjectREFR *ref = nullptr;  // what was struck, when it has a reference
        };

        // Cast a ray between two world-space points (game units). a_layer selects the
        // collision filter; kLOS collides with terrain/static geometry (what blocks
        // sight and stands as walkable ground) - and, run 5 showed, with actors too.
        RayHit CastRay(RE::Actor *a_actor, const RE::NiPoint3 &a_from, const RE::NiPoint3 &a_to,
                       RE::COL_LAYER a_layer = RE::COL_LAYER::kLOS);

        // True if a_self has a clear line of sight to a_target (no wall/railing/pillar
        // between their torsos). a_eyeHeight is the vertical offset from feet to the
        // sighting point. Fails open (returns true) on any access error.
        bool HasLineOfSight(RE::Actor *a_self, RE::Actor *a_target, float a_eyeHeight);

        // True if there is solid ground within a_maxDrop below the point a_horizDist
        // ahead of the actor along a_horizDir (a horizontal unit vector). Used to veto
        // a dodge that would carry the actor over a ledge. Fails open (returns true =
        // assume ground) so sensing errors never break dodging.
        bool HasGroundAhead(RE::Actor *a_actor, const RE::NiPoint3 &a_horizDir, float a_horizDist, float a_maxDrop);
    } // namespace EnvSensor
} // namespace CombatAI
