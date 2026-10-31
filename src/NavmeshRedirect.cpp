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
#include <mutex>
#include <vector>

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

        // Goals that got no path (NoteNoPath), for every NPC: a later Advance goal within kBadRadius of one moves
        // kAvoidSteps toward the NPC, onto navmesh outside every bad radius. shortcut: by distance, not by triangle
        // (no triangle lookup in CommonLib; tri 287 spanned ~300 units) - a goal still in the bad patch fails, gets
        // remembered too, and the next one moves on from there
        struct BadGoal
        {
            RE::NiPoint3 point;
            Clock::time_point at;
        };
        std::mutex g_badLock;
        std::vector<BadGoal> g_bad;
        constexpr float kBadMemory = 15.0f;
        constexpr float kBadRadius = 120.0f;
        constexpr float kAvoidSteps[] = {140.0f, 220.0f};

        bool NearBadGoal(const RE::NiPoint3 &a_point)
        {
            const auto now = Clock::now();
            std::scoped_lock guard(g_badLock);
            std::erase_if(g_bad, [&](const BadGoal &a_b) { return std::chrono::duration<float>(now - a_b.at).count() > kBadMemory; });
            return std::ranges::any_of(g_bad, [&](const BadGoal &a_b) { return a_b.point.GetDistance(a_point) < kBadRadius; });
        }

        float Distance2D(const RE::NiPoint3 &a_a, const RE::NiPoint3 &a_b)
        {
            return std::hypot(a_a.x - a_b.x, a_a.y - a_b.y);
        }

        bool OnNavmesh(RE::Actor *a_actor, const RE::NiPoint3 &a_point);
        bool AvoidBadGoal(std::uintptr_t a_path, RE::NiPoint3 &a_point, float &a_radius);

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
                    // An NPC standing off the navmesh has no start triangle: no path to any arc point (1.12.2 run: 5 of
                    // 10 failed Advances), so it takes the plain Advance
                    const auto actor = reinterpret_cast<const RE::ActorHandle *>(a_path + 0x10)->get();
                    if (actor && RingSlots::Goal(actor.get(), newPoint) && OnNavmesh(actor.get(), actor->GetPosition())) {
                        newRadius = kRingRadius;
                        swap = true;
                        Redirect(a_path, K, newPoint, newRadius, true);
                        newRadius = (std::max)(newRadius, kMinRadius);
                    }
                }
                swap = swap || Redirect(a_path, K, newPoint, newRadius);
                if constexpr (K == Kind::Advance) {
                    swap = AvoidBadGoal(a_path, newPoint, newRadius) || swap;
                }
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

        // a_point has a navmesh triangle under it within the game's limits (the default lookup's closest point at
        // most kOnNavmesh to the side, the point at most kMaxAbove above it and kMaxBelow below it)
        bool OnNavmesh(RE::Actor *a_actor, const RE::NiPoint3 &a_point)
        {
            auto *pathing = RE::Pathing::GetSingleton();
            RE::BSTSmartPointer<RE::BSPathingCell> cell;
            RE::NiPoint3 closest;
            if (!pathing || !pathing->GetPathingCell(a_point, a_actor->GetParentCell(), a_actor->GetWorldspace(), cell) || !cell ||
                !pathing->FindClosestPointOnNavmesh(cell, a_point, closest)) {
                return false;
            }
            const float above = a_point.z - closest.z;
            return Distance2D(closest, a_point) <= kOnNavmesh && above <= kMaxAbove && -above <= kMaxBelow;
        }

        // An Advance goal near a remembered no-path goal moves toward the NPC (kAvoidSteps), snapped to the navmesh, to
        // the first spot outside every bad radius. Not when the NPC itself stands in one (its start is the problem) or
        // is closer to the goal than the step.
        bool AvoidBadGoal(std::uintptr_t a_path, RE::NiPoint3 &a_point, float &a_radius)
        {
            if (!Config::GetInstance().GetDecisionMatrix().advanceNavmeshRedirect || !NearBadGoal(a_point)) {
                return false;
            }
            const auto actor = reinterpret_cast<const RE::ActorHandle *>(a_path + 0x10)->get();
            if (!actor || actor->IsPlayerRef()) {
                return false;
            }
            const auto from = actor->GetPosition();
            const bool debug = Config::GetInstance().IsDebugTarget(actor->GetFormID());
            const float dist = a_point.GetDistance(from);
            if (NearBadGoal(from)) {
                if (debug) {
                    LOG_DEBUG("Navmesh 0x{:08X}: Advance goal ({:.0f}, {:.0f}, {:.0f}) got no path before, but the NPC stands "
                              "there too - left as is",
                              actor->GetFormID(), a_point.x, a_point.y, a_point.z);
                }
                return false;
            }
            auto *pathing = RE::Pathing::GetSingleton();
            for (const float step : kAvoidSteps) {
                if (!pathing || dist < step + 40.0f) {
                    break;
                }
                const RE::NiPoint3 candidate = a_point + (from - a_point) * (step / dist);
                RE::BSTSmartPointer<RE::BSPathingCell> cell;
                RE::NiPoint3 closest;
                if (!pathing->GetPathingCell(candidate, actor->GetParentCell(), actor->GetWorldspace(), cell) || !cell ||
                    !pathing->FindClosestPointOnNavmesh(cell, candidate, closest)) {
                    continue;
                }
                const float above = candidate.z - closest.z;
                if (Distance2D(closest, candidate) > kOnNavmesh || above > kMaxAbove || -above > kMaxBelow || NearBadGoal(closest)) {
                    continue;
                }
                if (debug) {
                    LOG_DEBUG("Navmesh 0x{:08X}: Advance goal ({:.0f}, {:.0f}, {:.0f}) got no path before; goal moved {:.0f} "
                              "toward the NPC to ({:.0f}, {:.0f}, {:.0f}), radius {:.0f} -> {:.0f}",
                              actor->GetFormID(), a_point.x, a_point.y, a_point.z, step, closest.x, closest.y, closest.z,
                              a_radius, kMinRadius);
                }
                a_point = closest;
                a_radius = kMinRadius;
                return true;
            }
            if (debug) {
                LOG_DEBUG("Navmesh 0x{:08X}: Advance goal ({:.0f}, {:.0f}, {:.0f}) got no path before; no other navmesh "
                          "toward the NPC ({:.0f} away) - left as is",
                          actor->GetFormID(), a_point.x, a_point.y, a_point.z, dist);
            }
            return false;
        }

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

    void NoteNoPath(RE::Actor *a_actor, const RE::NiPoint3 &a_goal)
    {
        if (!a_actor || !Config::GetInstance().GetDecisionMatrix().advanceNavmeshRedirect) {
            return;
        }
        const auto now = Clock::now();
        {
            std::scoped_lock guard(g_badLock);
            // The same spot again refreshes its entry
            auto it = std::ranges::find_if(g_bad, [&](const BadGoal &a_b) { return a_b.point.GetDistance(a_goal) < kBadRadius * 0.5f; });
            if (it != g_bad.end()) {
                it->at = now;
            } else {
                if (g_bad.size() >= 32) {
                    g_bad.erase(g_bad.begin());
                }
                g_bad.push_back({a_goal, now});
            }
        }
        std::size_t count = 0;
        {
            std::scoped_lock guard(g_badLock);
            count = g_bad.size();
        }
        // Every NPC's: notes are rare (one per instant no-path failure) and tell who fills the memory
        LOG_DEBUG("Navmesh 0x{:08X}: Advance got no path to ({:.0f}, {:.0f}, {:.0f}) - Advance goals within {:.0f} of it "
                  "aim elsewhere for {:.0f} s ({} remembered)",
                  a_actor->GetFormID(), a_goal.x, a_goal.y, a_goal.z, kBadRadius, kBadMemory, count);
    }

    void NotePathWalked(RE::Actor *a_actor, const RE::NiPoint3 &a_goal)
    {
        if (!a_actor) {
            return;
        }
        std::size_t cleared = 0, left = 0;
        {
            std::scoped_lock guard(g_badLock);
            cleared = std::erase_if(g_bad, [&](const BadGoal &a_b) { return a_b.point.GetDistance(a_goal) < kBadRadius; });
            left = g_bad.size();
        }
        if (cleared) {
            LOG_DEBUG("Navmesh 0x{:08X}: Advance walked to ({:.0f}, {:.0f}, {:.0f}) - {} remembered no-path goal(s) near it "
                      "cleared ({} left)",
                      a_actor->GetFormID(), a_goal.x, a_goal.y, a_goal.z, cleared, left);
        }
    }

    void Evict(RE::FormID a_formID)
    {
        g_failed.Erase(a_formID);
    }
} // namespace CombatAI::NavmeshRedirect
