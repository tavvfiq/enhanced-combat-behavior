// Advance goal on the navmesh: see NavmeshRedirect.h. Offsets and IDs from SE 1.5.97 and AE 1.7.104 disassembly
// (D:\Modding\skyrim_re\COMBAT_AI_RE.md, "Redirecting Advance's goal to the navmesh").
#include "NavmeshRedirect.h"
#include "ActorUtils.h"
#include "Config.h"
#include "Logger.h"
#include "RingSlots.h"
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
        constexpr float kRingRadius = 48.0f;  // goal radius of a ring move (an arc point, not an exact spot)
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

        // The three combat paths whose goal can sit off the navmesh, with their destination's layout. The path's actor
        // handle sits at path+0x10 (CombatPath). A Ref destination (Advance, Flank) holds the target's point at +8 and
        // the goal radius at +0x20; a Location destination (Flank distant) its point at +0 and radius at +0x18. The
        // request takes the radius when >= 0 (FillRequest SE 49063 / AE 50011, SE 49056 / AE 50004).
        enum class Kind
        {
            Advance,
            Flank,
            FlankDistant
        };
        constexpr const char *kKindName[] = {"Advance after a failed one", "Flank", "Flank (distant)"};

        // When the goal point has no navmesh triangle, a_point becomes the closest navmesh point within the configured
        // distance and a_radius shrinks so the NPC walks onto it. Returns whether it changed them. An Advance is only
        // redirected after one of the NPC's Advances failed; a flank always (its failures aren't tracked, and a goal
        // off the navmesh fails anyway). Flanks log only what they change or refuse, not goals on the navmesh.
        bool Redirect(std::uintptr_t a_path, Kind a_kind, RE::NiPoint3 &a_point, float &a_radius, bool a_ring = false)
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
            if (a_kind == Kind::Advance && !a_ring) {
                const auto failed = g_failed.Find(id);
                if (!failed || std::chrono::duration<float>(Clock::now() - *failed).count() > kFailMemory) {
                    return false;
                }
            }
            if (const auto *state = ActorUtils::SafeAsActorState(actor.get()); state && state->IsFlying()) {
                return false;
            }
            auto *pathing = RE::Pathing::GetSingleton();
            if (!pathing) {
                return false;
            }
            const bool debug = Config::GetInstance().IsDebugTarget(id);
            const char *what = a_ring ? "Ring move" : kKindName[static_cast<int>(a_kind)];
            RE::BSTSmartPointer<RE::BSPathingCell> cell;
            if (!pathing->GetPathingCell(a_point, actor->GetParentCell(), actor->GetWorldspace(), cell) || !cell) {
                if (debug) {
                    LOG_DEBUG("Navmesh 0x{:08X}: {}: no pathing cell at the goal - left as is", static_cast<std::uint32_t>(id),
                              what);
                }
                return false;
            }
            // The game's own lookup (a triangle up to 180 above or 64 below the point), then a wider one
            RE::NiPoint3 closest;
            const char *filter = "default";
            if (pathing->FindClosestPointOnNavmesh(cell, a_point, closest)) {
                const float above = a_point.z - closest.z; // the goal above the navmesh (negative: below it)
                if (Distance2D(closest, a_point) <= kOnNavmesh && above <= kMaxAbove && -above <= kMaxBelow) {
                    if (debug && a_kind == Kind::Advance && !a_ring) {
                        LOG_DEBUG("Navmesh 0x{:08X}: {}: the goal ({:.0f}, {:.0f}, {:.0f}) is on the navmesh (closest point "
                                  "{:.0f} away, {:+.0f} high) - left as is",
                                  static_cast<std::uint32_t>(id), what, a_point.x, a_point.y, a_point.z,
                                  Distance2D(closest, a_point), closest.z - a_point.z);
                    }
                    return false;
                }
            } else {
                RE::FindTriangleForLocationFilterCheckDeltaZ wide;
                wide.maxDistAbove = kWideFilter;
                wide.maxDistBelow = kWideFilter;
                if (!pathing->FindClosestPointOnNavmesh(cell, a_point, wide, closest)) {
                    if (debug) {
                        LOG_DEBUG("Navmesh 0x{:08X}: {}: no navmesh near the goal ({:.0f}, {:.0f}, {:.0f}), not even {:.0f} "
                                  "above or below - left as is",
                                  static_cast<std::uint32_t>(id), what, a_point.x, a_point.y, a_point.z, kWideFilter);
                    }
                    return false;
                }
                filter = "wide";
            }
            const float off = Distance2D(closest, a_point);
            if (off > matrix.advanceRedirectMaxDistance) {
                if (debug) {
                    LOG_DEBUG("Navmesh 0x{:08X}: {}: closest navmesh point ({} lookup) {:.0f} from the goal, over {:.0f} - "
                              "left as is",
                              static_cast<std::uint32_t>(id), what, filter, off, matrix.advanceRedirectMaxDistance);
                }
                return false;
            }
            if (debug) {
                LOG_DEBUG("Navmesh 0x{:08X}: {}: the goal ({:.0f}, {:.0f}, {:.0f}) has no navmesh triangle; goal moved to "
                          "({:.0f}, {:.0f}, {:.0f}), {:.0f} away, {:+.0f} high ({} lookup), radius {:.0f} -> {:.0f}",
                          static_cast<std::uint32_t>(id), what, a_point.x, a_point.y, a_point.z, closest.x, closest.y,
                          closest.z, off, closest.z - a_point.z, filter, a_radius, kMinRadius);
            }
            a_point = closest;
            a_radius = kMinRadius;
            return true;
        }

        // Slot 7 of a combat path class: points the destination at a_path + Dest and fills the request's goal from it.
        // The destination is swapped only for this call and put back after: its update keeps comparing against the
        // real goal, so the path still restarts when the target moves.
        template <Kind K, std::ptrdiff_t Dest, std::ptrdiff_t Point, std::ptrdiff_t Radius>
        struct FillRequest
        {
            static void thunk(std::uintptr_t a_path)
            {
                auto *point = reinterpret_cast<RE::NiPoint3 *>(a_path + Dest + Point);
                auto *radius = reinterpret_cast<float *>(a_path + Dest + Radius);
                const RE::NiPoint3 savedPoint = *point;
                const float savedRadius = *radius;
                RE::NiPoint3 newPoint = savedPoint;
                float newRadius = savedRadius;
                bool swap = false;
                if constexpr (K == Kind::Advance) {
                    // Ring slots: an Advance toward the NPC's sector goes to its arc point (then the navmesh check on it)
                    const auto actor = reinterpret_cast<const RE::ActorHandle *>(a_path + 0x10)->get();
                    if (actor && RingSlots::Goal(actor.get(), newPoint)) {
                        newRadius = kRingRadius;
                        swap = true;
                        Redirect(a_path, K, newPoint, newRadius, true);
                        newRadius = (std::max)(newRadius, kMinRadius);
                    }
                }
                swap = swap || Redirect(a_path, K, newPoint, newRadius);
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

        template <class Hook>
        void HookSlot7(REL::RelocationID a_vtable)
        {
            REL::Relocation<std::uintptr_t> vtable{a_vtable};
            Hook::func = vtable.write_vfunc(7, Hook::thunk);
        }
    } // namespace

    void Install()
    {
        // CombatPathT<CombatPathRequestGeneric<GoalPolicy, SearchPolicy>, Destination> vtables (slot 7 jumps to the
        // destination's FillRequest with dest = path + Dest; AE layouts checked against 1.7.104):
        //   Advance        FindTargetLocation / Standard, Ref:  SE 265576, AE 212080, dest +0x88
        //   Flank          Flank / Flank, Ref:                  SE 266331, AE 213017, dest +0xD8
        //   Flank distant  FlankDistant / Standard, Location:   SE 266329, AE 213009, dest +0x78
        HookSlot7<FillRequest<Kind::Advance, 0x88, 0x08, 0x20>>(REL::RelocationID(265576, 212080));
        HookSlot7<FillRequest<Kind::Flank, 0xD8, 0x08, 0x20>>(REL::RelocationID(266331, 213017));
        HookSlot7<FillRequest<Kind::FlankDistant, 0x78, 0x00, 0x18>>(REL::RelocationID(266329, 213009));
        LOG_INFO("Navmesh: goal redirect hooked (Advance after a failed one, Flank, Flank distant: a goal off the navmesh "
                 "is moved to the closest navmesh point)");
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
