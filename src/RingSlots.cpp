// Ring slots: see RingSlots.h.
#include "RingSlots.h"
#include "ActorStateData.h"
#include "Config.h"
#include "Logger.h"
#include "ThreadSafeMap.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <numbers>
#include <unordered_map>
#include <vector>

namespace CombatAI::RingSlots
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        constexpr float kPi = std::numbers::pi_v<float>;
        constexpr float kDeg = kPi / 180.0f;

        constexpr float kStale = 1.5f;               // seconds without a brain tick: off the ring
        constexpr float kSolveEvery = 0.5f;          // seconds between re-solves of a target's sectors
        constexpr float kLopsided = 2.0f;            // even out once the biggest gap is this many times the smallest
        constexpr float kSwapBeyond = 90.0f * kDeg;  // farther than this from its sector: trade with a closer NPC
        constexpr float kSwapGain = 60.0f * kDeg;    // ...when the pair's walking shrinks by at least this much
        constexpr float kSwapCooldown = 5.0f;        // seconds before an NPC that traded may trade again
        constexpr float kDriftAfter = 30.0f * kDeg;  // the layout turns toward where the group stands once this far off
        constexpr float kDriftStep = 10.0f * kDeg;   // ...by at most this per solve (kSolveEvery)
        constexpr float kApproachTolerance = 30.0f * kDeg; // approaching inside this of its sector: straight in
        constexpr float kWaitTolerance = 20.0f * kDeg;     // waiting inside this of its sector: circle as before
        constexpr float kArcStep = 50.0f * kDeg;           // each leg of the walk around the target
        constexpr float kArcFrom = 600.0f;                 // farther than this an approach walks straight in first
        constexpr float kArcMaxRadius = 450.0f;            // ...and its arc runs no farther out than this
        constexpr float kArrived = 60.0f;                  // this close to its arc point it is there (no new goal)
        constexpr float kLongWalk = 90.0f * kDeg;          // farther than this from its sector an approach goes straight in
        constexpr float kCrowdAngle = 30.0f * kDeg;        // two NPCs on one ring this close in bearing...
        constexpr float kCrowdDistance = 80.0f;            // ...and in distance crowd each other (close spacing)
        constexpr float kStepAngle = 35.0f * kDeg;         // a side step: at least this much around the target
        constexpr float kStepArc = 80.0f;                  // ...and at least this long
        constexpr float kGoalTTL = 0.5f;                   // a ring goal lives this long without a new Steer

        // Angle difference a - b wrapped to (-pi, pi]
        float Wrap(float a_angle)
        {
            while (a_angle > kPi) {
                a_angle -= 2.0f * kPi;
            }
            while (a_angle <= -kPi) {
                a_angle += 2.0f * kPi;
            }
            return a_angle;
        }

        // World bearing of a_point seen from a_center (radians, atan2(dy, dx))
        float Bearing(const RE::NiPoint3 &a_center, const RE::NiPoint3 &a_point)
        {
            return std::atan2(a_point.y - a_center.y, a_point.x - a_center.x);
        }

        struct Member
        {
            RE::FormID id = 0;
            float bearing = 0.0f; // where it stands now
            float sector = 0.0f;  // where it should be
            float distance = 0.0f; // from the target
            Clock::time_point seen{};
            Clock::time_point traded{}; // its last sector trade
        };

        struct Ring
        {
            std::vector<Member> members;
            bool solved = false; // two or more members: sectors steer
            Clock::time_point solvedAt{};
        };

        std::mutex g_lock; // brain thread (Note, Steer); Evict from the director
        std::unordered_map<RE::FormID, Ring> g_rings;        // by target
        std::unordered_map<RE::FormID, RE::FormID> g_target; // member -> target

        struct Goal
        {
            RE::NiPoint3 point;
            Clock::time_point until{};
        };
        ThreadSafeMap<RE::FormID, Goal> g_goals; // read on the combat AI threads (Advance path fill)

        struct Told
        {
            int mode = -1;
            float sector = 0.0f;
            Clock::time_point at{};
        };
        std::unordered_map<RE::FormID, Told> g_told; // debug lines for the selected NPC

        // Sectors stay where they were given (1.12.0 refit them to where the NPCs stood: the selected NPC's sector
        // moved a median 16 degrees between log lines, 30 of 190 times over 30, and 36 of 192 lines sent it over 90
        // degrees around the target).
        // A newcomer takes the middle of the biggest free gap; nobody else moves.
        float NewSector(const Ring &a_ring, float a_bearing)
        {
            std::vector<float> sectors;
            for (const auto &m : a_ring.members) {
                sectors.push_back(m.sector);
            }
            if (sectors.empty()) {
                return a_bearing;
            }
            std::ranges::sort(sectors);
            float best = 0.0f, middle = a_bearing;
            for (std::size_t k = 0; k < sectors.size(); ++k) {
                const float from = sectors[k];
                float gap = (k + 1 < sectors.size() ? sectors[k + 1] : sectors[0] + 2.0f * kPi) - from;
                if (sectors.size() == 1) {
                    gap = 2.0f * kPi;
                }
                if (gap > best) {
                    best = gap;
                    middle = Wrap(from + gap * 0.5f);
                }
            }
            return middle;
        }

        // Every kSolveEvery: when the gaps are lopsided (someone left), even them out keeping the order, each moving
        // as little as it can (circular mean of sector minus slot angle). The layout then turns toward where the group
        // stands (the target moves and circles: 1.12.1 run, NPCs pushed 110-168 degrees off their fixed sectors walked
        // back around under its weapon), at most kDriftStep a solve and only once it is kDriftAfter off: the spacing
        // and order hold, the layout follows slowly. Then one trade: an NPC farther than kSwapBeyond from its sector
        // swaps with the NPC whose sector is nearest to where it stands, if that saves the pair kSwapGain of walking;
        // neither trades again for kSwapCooldown (1.12.1: two NPCs traded back and forth three times in 35 s).
        void Solve(Ring &a_ring, Clock::time_point a_now)
        {
            auto &m = a_ring.members;
            std::ranges::sort(m, {}, &Member::sector);
            const std::size_t n = m.size();
            const float step = 2.0f * kPi / static_cast<float>(n);
            float smallest = 2.0f * kPi, biggest = 0.0f;
            for (std::size_t k = 0; k < n; ++k) {
                const float gap = (k + 1 < n ? m[k + 1].sector : m[0].sector + 2.0f * kPi) - m[k].sector;
                smallest = (std::min)(smallest, gap);
                biggest = (std::max)(biggest, gap);
            }
            if (biggest > kLopsided * (std::max)(smallest, 1.0f * kDeg)) {
                float sx = 0.0f, sy = 0.0f;
                for (std::size_t k = 0; k < n; ++k) {
                    const float a = m[k].sector - step * static_cast<float>(k);
                    sx += std::cos(a);
                    sy += std::sin(a);
                }
                const float offset = std::atan2(sy, sx);
                for (std::size_t k = 0; k < n; ++k) {
                    m[k].sector = Wrap(offset + step * static_cast<float>(k));
                }
            }
            float dx = 0.0f, dy = 0.0f;
            for (const auto &e : m) {
                const float d = Wrap(e.bearing - e.sector);
                dx += std::cos(d);
                dy += std::sin(d);
            }
            if (const float drift = std::atan2(dy, dx); std::fabs(drift) > kDriftAfter) {
                const float turn = std::clamp(drift, -kDriftStep, kDriftStep);
                for (auto &e : m) {
                    e.sector = Wrap(e.sector + turn);
                }
            }
            const auto cooled = [&](const Member &a_m) {
                return std::chrono::duration<float>(a_now - a_m.traded).count() >= kSwapCooldown;
            };
            for (auto &a : m) {
                if (std::fabs(Wrap(a.sector - a.bearing)) <= kSwapBeyond || !cooled(a)) {
                    continue;
                }
                Member *nearest = nullptr;
                for (auto &b : m) {
                    if (&b != &a && cooled(b) &&
                        (!nearest || std::fabs(Wrap(b.sector - a.bearing)) < std::fabs(Wrap(nearest->sector - a.bearing)))) {
                        nearest = &b;
                    }
                }
                if (!nearest) {
                    continue;
                }
                const float now = std::fabs(Wrap(a.sector - a.bearing)) + std::fabs(Wrap(nearest->sector - nearest->bearing));
                const float swapped = std::fabs(Wrap(nearest->sector - a.bearing)) + std::fabs(Wrap(a.sector - nearest->bearing));
                if (now - swapped >= kSwapGain) {
                    std::swap(a.sector, nearest->sector);
                    a.traded = nearest->traded = a_now;
                    break;
                }
            }
            a_ring.solved = true;
        }

        void SetGoal(RE::FormID a_id, const RE::NiPoint3 &a_point)
        {
            const auto until = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(kGoalTTL));
            g_goals.ModifyOrCreate(a_id, [&](Goal &a_goal) {
                a_goal.point = a_point;
                a_goal.until = until;
            });
        }
    } // namespace

    void Note(RE::Actor *a_actor, RE::Actor *a_target)
    {
        if (!a_actor || !a_target) {
            return;
        }
        const auto id = a_actor->GetFormID();
        const auto targetId = a_target->GetFormID();
        const auto now = Clock::now();
        const float bearing = Bearing(a_target->GetPosition(), a_actor->GetPosition());
        const auto tp = a_target->GetPosition(), sp = a_actor->GetPosition();
        const float distance = std::hypot(sp.x - tp.x, sp.y - tp.y);
        std::scoped_lock guard(g_lock);
        // Switched targets: off the old ring
        if (auto it = g_target.find(id); it != g_target.end() && it->second != targetId) {
            if (auto ring = g_rings.find(it->second); ring != g_rings.end()) {
                std::erase_if(ring->second.members, [&](const Member &a_m) { return a_m.id == id; });
            }
        }
        g_target[id] = targetId;
        auto &ring = g_rings[targetId];
        auto member = std::ranges::find(ring.members, id, &Member::id);
        if (member == ring.members.end()) {
            ring.members.push_back({id, bearing, NewSector(ring, bearing), distance, now, {}});
        } else {
            member->bearing = bearing;
            member->distance = distance;
            member->seen = now;
        }
        std::erase_if(ring.members, [&](const Member &a_m) {
            return std::chrono::duration<float>(now - a_m.seen).count() > kStale;
        });
        if (std::chrono::duration<float>(now - ring.solvedAt).count() >= kSolveEvery) {
            ring.solvedAt = now;
            if (ring.members.size() >= 2) {
                Solve(ring, now);
            } else {
                ring.solved = false;
            }
        }
    }

    void Steer(RE::Actor *a_actor, DecisionResult &a_decision, const ActorStateData &a_state)
    {
        if (!a_actor || !Config::GetInstance().GetCombatPacing().enableRingSlots || !a_state.target.isValid ||
            a_state.self.isRanged) {
            return;
        }
        const bool waiting = a_decision.action == ActionType::Flanking && a_decision.holdDistance > 0.0f;
        const bool approaching = a_decision.action == ActionType::Advancing;
        const auto id = a_actor->GetFormID();
        if (!waiting && !approaching) {
            g_goals.Erase(id);
            return;
        }
        float sector = 0.0f;
        std::size_t count = 0;
        // Close spacing: the nearest NPC on the same ring standing on top of this one (within kCrowdAngle and
        // kCrowdDistance), and whether this one is the one to move (the farther from its sector; ties by FormID)
        RE::FormID crowder = 0;
        bool yields = false;
        float crowderBearing = 0.0f;
        {
            std::scoped_lock guard(g_lock);
            const auto t = g_target.find(id);
            const auto ring = t != g_target.end() ? g_rings.find(t->second) : g_rings.end();
            if (ring == g_rings.end() || !ring->second.solved) {
                g_goals.Erase(id);
                return;
            }
            const auto member = std::ranges::find(ring->second.members, id, &Member::id);
            if (member == ring->second.members.end()) {
                g_goals.Erase(id);
                return;
            }
            sector = member->sector;
            count = ring->second.members.size();
            const float myOff = std::fabs(Wrap(member->sector - member->bearing));
            for (const auto &other : ring->second.members) {
                if (other.id == id || std::fabs(Wrap(other.bearing - member->bearing)) >= kCrowdAngle ||
                    std::fabs(other.distance - member->distance) >= kCrowdDistance) {
                    continue;
                }
                const float otherOff = std::fabs(Wrap(other.sector - other.bearing));
                crowder = other.id;
                crowderBearing = other.bearing;
                yields = myOff > otherOff || (myOff == otherOff && id > other.id);
                break;
            }
        }
        const auto center = a_state.target.position;
        const float bearing = Bearing(center, a_state.self.position);
        const float off = Wrap(sector - bearing);
        const float dist = a_state.target.distance;
        const float reach = a_state.weaponReach > 0.0f ? a_state.weaponReach : 150.0f;
        // Approaching: walk the arc a bit inside the current distance until in its sector, then straight in. Inside
        // 2 x reach (at least reach + 150) it just fights or closes straight in: no walking around under the target's
        // weapon (1.12.1: at 1.5 x reach it flipped between the two every 0.2 s at 107-124 units).
        // Waiting: walk to its sector at the wait ring; once there, circle as before.
        float radius = 0.0f;
        const bool close = dist <= (std::max)(reach * 2.0f, reach + 150.0f);
        // Far out it walks straight in first (1.12.2 run: arcs at 600-790 units walked around the target instead of
        // closing); the arc then spirals inward. More than kLongWalk from its sector it walks straight in too: the
        // target moving and turning put NPCs 90-180 degrees off, and walking that half lap at 300-600 units was the
        // most common far-off case (1.13.x runs: 79 of 160 lines over 90 off); trades and drift realign the sectors,
        // and close spacing sorts out who stands where
        const bool longWalk = std::fabs(off) > kLongWalk;
        if (approaching && std::fabs(off) > kApproachTolerance && !close && dist <= kArcFrom && !longWalk) {
            radius = std::clamp(dist * 0.7f, reach + 60.0f, (std::min)(dist, kArcMaxRadius));
        } else if (waiting && std::fabs(off) > kWaitTolerance) {
            radius = a_decision.holdDistance;
        }
        const bool inSector = std::fabs(off) <= (waiting ? kWaitTolerance : kApproachTolerance);
        // Close in, crowding another NPC: the one farther from its sector steps aside at the same distance, toward its
        // sector (or away from the other when already in it, or more than kLongWalk from it: near 180 off the side
        // flips with every small move), far enough to be a real step (>= 80 units of arc).
        // 1.12.3 run: 2+ crowded pairs in 22 of 58 samples although the sectors were spread.
        float sideStep = 0.0f;
        if (close && radius <= 0.0f && crowder != 0 && yields && Config::GetInstance().GetCombatPacing().enableCloseSpacing) {
            const float side = std::fabs(off) > 5.0f * kDeg && !longWalk ? (off > 0.0f ? 1.0f : -1.0f)
                                                                         : (Wrap(bearing - crowderBearing) >= 0.0f ? 1.0f : -1.0f);
            const float minStep = 2.0f * std::asin((std::min)(1.0f, kStepArc / (2.0f * (std::max)(dist, 1.0f))));
            sideStep = side * (std::max)(kStepAngle, minStep);
            radius = (std::max)(dist, reach * 0.8f);
        }
        int mode = radius > 0.0f ? (waiting ? 2 : 1) : (inSector ? 0 : 3);
        if (sideStep != 0.0f) {
            mode = 4;
        } else if (mode == 3 && approaching && dist > kArcFrom) {
            mode = 5;
        } else if (mode == 3 && approaching && longWalk && !close) {
            mode = 6;
        }
        if (Config::GetInstance().IsDebugTarget(id)) {
            auto &told = g_told[id];
            const auto now = Clock::now();
            if (told.mode != mode || std::fabs(Wrap(told.sector - sector)) > 5.0f * kDeg ||
                now - told.at > std::chrono::seconds(3)) {
                told = {mode, sector, now};
                constexpr const char *kMode[] = {"in its sector", "approaching around the target", "waiting: moving to its sector",
                                                 "close: fights from where it stands", "close: stepping aside from a crowding ally",
                                                 "far: walks straight in", "far off its side: walks straight in"};
                LOG_DEBUG("Ring 0x{:08X}: sector {:.0f} of {} (now at {:.0f}, {:+.0f} off, dist {:.0f}) - {}{}{}",
                          static_cast<std::uint32_t>(id), sector / kDeg, count, bearing / kDeg, off / kDeg, dist, kMode[mode],
                          radius > 0.0f ? std::format(", arc goal at {:.0f}", radius) : std::string(),
                          crowder ? std::format(" (0x{:08X} within {:.0f} deg)", static_cast<std::uint32_t>(crowder),
                                                std::fabs(Wrap(crowderBearing - bearing)) / kDeg)
                                  : std::string());
            }
        }
        if (radius <= 0.0f) {
            g_goals.Erase(id);
            return;
        }
        const float legAngle = sideStep != 0.0f ? bearing + sideStep : bearing + std::clamp(off, -kArcStep, kArcStep);
        const RE::NiPoint3 goal(center.x + radius * std::cos(legAngle), center.y + radius * std::sin(legAngle), center.z);
        // Already standing on its arc point: a path there fails ("no path" on the start triangle, 1.12.2: 2 of 10)
        if (std::hypot(goal.x - a_state.self.position.x, goal.y - a_state.self.position.y) < kArrived) {
            g_goals.Erase(id);
            return;
        }
        SetGoal(id, goal);
        if (waiting) {
            // The Advance node walks it there (Circle can't go far: view cone, spots reserved)
            a_decision.action = ActionType::Advancing;
        }
    }

    bool Goal(RE::Actor *a_actor, RE::NiPoint3 &a_point)
    {
        if (!a_actor) {
            return false;
        }
        const auto goal = g_goals.Find(a_actor->GetFormID());
        if (!goal || Clock::now() > goal->until) {
            return false;
        }
        a_point = goal->point;
        return true;
    }

    void Evict(RE::FormID a_formID)
    {
        g_goals.Erase(a_formID);
        std::scoped_lock guard(g_lock);
        if (auto it = g_target.find(a_formID); it != g_target.end()) {
            if (auto ring = g_rings.find(it->second); ring != g_rings.end()) {
                std::erase_if(ring->second.members, [&](const Member &a_m) { return a_m.id == a_formID; });
                if (ring->second.members.empty()) {
                    g_rings.erase(ring);
                }
            }
            g_target.erase(it);
        }
        g_told.erase(a_formID);
    }
} // namespace CombatAI::RingSlots
