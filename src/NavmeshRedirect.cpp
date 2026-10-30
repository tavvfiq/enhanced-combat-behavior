// Advance goal on the navmesh: see NavmeshRedirect.h. Offsets and IDs from SE 1.5.97 and AE 1.7.104 disassembly
// (D:\Modding\skyrim_re\COMBAT_AI_RE.md, "Redirecting Advance's goal to the navmesh").
#include "NavmeshRedirect.h"
#include "ActorUtils.h"
#include "Config.h"
#include "Logger.h"
#include "ThreadSafeMap.h"

#include <chrono>
#include <cmath>

namespace CombatAI::NavmeshRedirect
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        constexpr float kFailMemory = 5.0f;  // seconds an Advance failure arms the redirect
        constexpr float kOnNavmesh = 16.0f;  // the target's spot is on the navmesh when the closest point is this close (2D)
        constexpr float kWideFilter = 256.0f; // second try: a triangle up to this far above or below the spot
        constexpr float kMinRadius = 16.0f;   // goal radius once redirected: walk onto the point, not near it
        // The game's default triangle filter (BSPathing ctor, SE + AE): a point is on a triangle when it is at most
        // 180 above it or 64 below it. The closest-point lookup returns a point outside these (1.10.0 run: 62 spots
        // with the navmesh 69-98 above the target's feet, 0 to the side), but the path search then finds no goal
        // triangle, so those count as off the navmesh
        constexpr float kMaxAbove = 180.0f;
        constexpr float kMaxBelow = 64.0f;

        ThreadSafeMap<RE::FormID, Clock::time_point> g_failed;

        float Distance2D(const RE::NiPoint3 &a_a, const RE::NiPoint3 &a_b)
        {
            return std::hypot(a_a.x - a_b.x, a_a.y - a_b.y);
        }

        // The Advance path's destination (CombatPathDestinationRef at path+0x88): the target's point at +8, the goal
        // radius at +0x20 (the request takes it when >= 0). The path's actor handle sits at path+0x10.
        // When the point has no navmesh triangle, a_point becomes the closest navmesh point within the configured
        // distance and a_radius shrinks so the NPC walks onto it. Returns whether it changed them.
        bool Redirect(std::uintptr_t a_path, RE::NiPoint3 &a_point, float &a_radius)
        {
            const auto &matrix = Config::GetInstance().GetDecisionMatrix();
            if (!matrix.advanceNavmeshRedirect) {
                return false;
            }
            const auto actor = reinterpret_cast<const RE::ActorHandle *>(a_path + 0x10)->get();
            if (!actor || actor->IsPlayerRef() || !actor->IsInCombat()) {
                return false;
            }
            const auto id = actor->GetFormID();
            const auto failed = g_failed.Find(id);
            if (!failed || std::chrono::duration<float>(Clock::now() - *failed).count() > kFailMemory) {
                return false;
            }
            if (const auto *state = ActorUtils::SafeAsActorState(actor.get()); state && state->IsFlying()) {
                return false;
            }
            auto *pathing = RE::Pathing::GetSingleton();
            if (!pathing) {
                return false;
            }
            const bool debug = Config::GetInstance().IsDebugTarget(id);
            RE::BSTSmartPointer<RE::BSPathingCell> cell;
            if (!pathing->GetPathingCell(a_point, actor->GetParentCell(), actor->GetWorldspace(), cell) || !cell) {
                if (debug) {
                    LOG_DEBUG("Navmesh 0x{:08X}: Advance after a failed one: no pathing cell at the target - left as is",
                              static_cast<std::uint32_t>(id));
                }
                return false;
            }
            // The game's own lookup (a triangle up to 180 above or 64 below the point), then a wider one
            RE::NiPoint3 closest;
            const char *filter = "default";
            if (pathing->FindClosestPointOnNavmesh(cell, a_point, closest)) {
                const float above = a_point.z - closest.z; // the target's feet above the navmesh (negative: below it)
                if (Distance2D(closest, a_point) <= kOnNavmesh && above <= kMaxAbove && -above <= kMaxBelow) {
                    if (debug) {
                        LOG_DEBUG("Navmesh 0x{:08X}: Advance after a failed one: the target's spot ({:.0f}, {:.0f}, {:.0f}) is on "
                                  "the navmesh (closest point {:.0f} away, {:+.0f} high) - left as is",
                                  static_cast<std::uint32_t>(id), a_point.x, a_point.y, a_point.z, Distance2D(closest, a_point),
                                  closest.z - a_point.z);
                    }
                    return false;
                }
            } else {
                RE::FindTriangleForLocationFilterCheckDeltaZ wide;
                wide.maxDistAbove = kWideFilter;
                wide.maxDistBelow = kWideFilter;
                if (!pathing->FindClosestPointOnNavmesh(cell, a_point, wide, closest)) {
                    if (debug) {
                        LOG_DEBUG("Navmesh 0x{:08X}: Advance after a failed one: no navmesh near the target's spot ({:.0f}, "
                                  "{:.0f}, {:.0f}), not even {:.0f} above or below - left as is",
                                  static_cast<std::uint32_t>(id), a_point.x, a_point.y, a_point.z, kWideFilter);
                    }
                    return false;
                }
                filter = "wide";
            }
            const float off = Distance2D(closest, a_point);
            if (off > matrix.advanceRedirectMaxDistance) {
                if (debug) {
                    LOG_DEBUG("Navmesh 0x{:08X}: Advance after a failed one: closest navmesh point ({} lookup) {:.0f} from the "
                              "target, over {:.0f} - left as is",
                              static_cast<std::uint32_t>(id), filter, off, matrix.advanceRedirectMaxDistance);
                }
                return false;
            }
            if (debug) {
                LOG_DEBUG("Navmesh 0x{:08X}: Advance after a failed one: the target's spot ({:.0f}, {:.0f}, {:.0f}) has no "
                          "navmesh triangle; goal moved to ({:.0f}, {:.0f}, {:.0f}), {:.0f} away, {:+.0f} high ({} lookup), "
                          "radius {:.0f} -> {:.0f}",
                          static_cast<std::uint32_t>(id), a_point.x, a_point.y, a_point.z, closest.x, closest.y, closest.z,
                          off, closest.z - a_point.z, filter, a_radius, kMinRadius);
            }
            a_point = closest;
            a_radius = kMinRadius;
            return true;
        }

        // Slot 7 of the Advance path class (SE 45897 / AE 47087: dest = path+0x88, then DestinationRef::FillRequest
        // SE 49063 / AE 50011 builds the request's goal from dest+8 and its radius from dest+0x20). The destination is
        // swapped only for this call and put back after: its update keeps comparing against the target's real
        // position, so the path still restarts when the target moves.
        struct FillRequest
        {
            static void thunk(std::uintptr_t a_path)
            {
                auto *point = reinterpret_cast<RE::NiPoint3 *>(a_path + 0x88 + 0x08);
                auto *radius = reinterpret_cast<float *>(a_path + 0x88 + 0x20);
                const RE::NiPoint3 savedPoint = *point;
                const float savedRadius = *radius;
                RE::NiPoint3 newPoint = savedPoint;
                float newRadius = savedRadius;
                const bool swap = Redirect(a_path, newPoint, newRadius);
                if (swap) {
                    *point = newPoint;
                    *radius = newRadius;
                }
                func(a_path);
                if (swap) {
                    *point = savedPoint;
                    *radius = savedRadius;
                }
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };
    } // namespace

    void Install()
    {
        // CombatPathT<CombatPathRequestGeneric<CombatPathingGoalPolicyFindTargetLocation,
        // CombatPathingSearchPolicyStandard>, CombatPathDestinationRef>: SE vtable id 265576, AE 212080
        REL::Relocation<std::uintptr_t> vtable{REL::RelocationID(265576, 212080)};
        FillRequest::func = vtable.write_vfunc(7, FillRequest::thunk);
        LOG_INFO("Navmesh: Advance goal redirect hooked (after a failed Advance, a target off the navmesh is approached at "
                 "the closest navmesh point)");
    }

    void NoteAdvanceFailed(RE::Actor *a_actor)
    {
        if (a_actor) {
            const auto now = Clock::now();
            g_failed.ModifyOrCreate(a_actor->GetFormID(), [&](Clock::time_point &a_at) { a_at = now; });
        }
    }

    void Evict(RE::FormID a_formID)
    {
        g_failed.Erase(a_formID);
    }
} // namespace CombatAI::NavmeshRedirect
