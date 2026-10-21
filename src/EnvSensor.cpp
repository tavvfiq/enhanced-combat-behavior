#include "EnvSensor.h"
#include "ActorUtils.h"
#include "Config.h"
#include "pch.h"

namespace CombatAI
{
    namespace EnvSensor
    {
        // Any hit closer than this fraction of the self->target ray counts as an
        // obstruction. Kept below 1.0 so geometry grazed right at the target's own
        // position doesn't read as a blocking wall.
        constexpr float kLosBlockFraction = 0.95f;

        RayHit CastRay(RE::Actor *a_actor, const RE::NiPoint3 &a_from, const RE::NiPoint3 &a_to, RE::COL_LAYER a_layer)
        {
            RayHit result;

            if (!a_actor) {
                return result;
            }

            RE::TESObjectCELL *cell = a_actor->GetParentCell();
            if (!cell) {
                return result;
            }

            RE::bhkWorld *world = cell->GetbhkWorld();
            if (!world) {
                return result;
            }

            // Havok works in its own scaled units, not game units.
            const float scale = RE::bhkWorld::GetWorldScale();

            RE::bhkPickData pick;
            pick.rayInput.from = RE::hkVector4(a_from.x * scale, a_from.y * scale, a_from.z * scale, 0.0f);
            pick.rayInput.to = RE::hkVector4(a_to.x * scale, a_to.y * scale, a_to.z * scale, 0.0f);
            pick.ray = RE::hkVector4((a_to.x - a_from.x) * scale, (a_to.y - a_from.y) * scale,
                                     (a_to.z - a_from.z) * scale, 0.0f);
            pick.rayInput.enableShapeCollectionFilter = false;
            pick.rayInput.filterInfo.SetCollisionLayer(a_layer);

            {
                // PickObject walks the broadphase; hold a read lock so a concurrent
                // physics step can't tear the world out from under us.
                RE::BSReadLockGuard lock(world->worldLock);
                world->PickObject(pick);
            }

            if (pick.rayOutput.HasHit()) {
                result.hit = true;
                result.fraction = pick.rayOutput.hitFraction;
                result.ref = RE::TESHavokUtilities::FindCollidableRef(*pick.rayOutput.rootCollidable);
            }

            return result;
        }

        bool HasLineOfSight(RE::Actor *a_self, RE::Actor *a_target, float a_eyeHeight)
        {
            if (!a_self || !a_target) {
                return true; // fail open
            }

            auto selfPos = ActorUtils::SafeGetPosition(a_self);
            auto targetPos = ActorUtils::SafeGetPosition(a_target);
            if (!selfPos.has_value() || !targetPos.has_value()) {
                return true; // fail open
            }

            RE::NiPoint3 from = selfPos.value();
            from.z += a_eyeHeight;
            RE::NiPoint3 to = targetPos.value();
            to.z += a_eyeHeight;

            RayHit hit = CastRay(a_self, from, to, RE::COL_LAYER::kLOS);

            // A static/terrain hit before the target's position = wall between us.
            // kLOS still hits actors (run 5: every in-range check was blocked, 102/102), and the
            // ray reports only the closest hit, so a body (self, target, bystander) is not a wall.
            const bool blocked = hit.hit && hit.fraction < kLosBlockFraction && !(hit.ref && hit.ref->As<RE::Actor>());
            if (blocked && Config::GetInstance().IsDebugTarget(a_self->GetFormID())) {
                LOG_DEBUG("LOS 0x{:08X}: blocked at {:.2f} of the ray by {} 0x{:08X}", a_self->GetFormID(),
                          hit.fraction, hit.ref ? hit.ref->GetName() : "geometry", hit.ref ? hit.ref->GetFormID() : 0);
            }
            return !blocked;
        }

        bool HasGroundAhead(RE::Actor *a_actor, const RE::NiPoint3 &a_horizDir, float a_horizDist, float a_maxDrop)
        {
            if (!a_actor) {
                return true; // fail open (assume ground)
            }

            auto pos = ActorUtils::SafeGetPosition(a_actor);
            if (!pos.has_value()) {
                return true; // fail open
            }

            RE::NiPoint3 landing = pos.value();
            landing.x += a_horizDir.x * a_horizDist;
            landing.y += a_horizDir.y * a_horizDist;

            // Probe straight down through the landing spot: start a little above the
            // actor's feet (to catch a small step-up) and end a_maxDrop below.
            RE::NiPoint3 from = landing;
            from.z += 40.0f;
            RE::NiPoint3 to = landing;
            to.z -= a_maxDrop;

            RayHit hit = CastRay(a_actor, from, to, RE::COL_LAYER::kLOS);

            // Ground found within the probe = safe to dodge there.
            return hit.hit;
        }
    } // namespace EnvSensor
} // namespace CombatAI
