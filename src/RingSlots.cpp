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
        constexpr float kKeepOffset = 15.0f * kDeg;  // same group: keep the old spread unless it moved this much
        constexpr float kApproachTolerance = 30.0f * kDeg; // approaching inside this of its sector: straight in
        constexpr float kWaitTolerance = 20.0f * kDeg;     // waiting inside this of its sector: circle as before
        constexpr float kArcStep = 50.0f * kDeg;           // each leg of the walk around the target
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
            Clock::time_point seen{};
        };

        struct Ring
        {
            std::vector<Member> members; // in sector order once solved
            std::vector<RE::FormID> roster; // sorted ids at the last solve
            float offset = 0.0f;
            bool solved = false;
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

        // Evenly spaced sectors, members kept in order. A new group (someone joined or left) is ordered by where
        // they stand now; the same group keeps its order, so two NPCs passing each other don't swap sectors. The spread
        // turns to fit where they stand (circular mean of bearing minus slot angle) and keeps its old turn unless
        // the new one is kKeepOffset off.
        void Solve(Ring &a_ring)
        {
            auto &m = a_ring.members;
            std::vector<RE::FormID> roster;
            roster.reserve(m.size());
            for (const auto &e : m) {
                roster.push_back(e.id);
            }
            std::ranges::sort(roster);
            const bool sameGroup = a_ring.solved && roster == a_ring.roster;
            if (!sameGroup) {
                std::ranges::sort(m, {}, &Member::bearing);
            }
            const float step = 2.0f * kPi / static_cast<float>(m.size());
            float sx = 0.0f, sy = 0.0f;
            for (std::size_t k = 0; k < m.size(); ++k) {
                const float a = m[k].bearing - step * static_cast<float>(k);
                sx += std::cos(a);
                sy += std::sin(a);
            }
            float offset = std::atan2(sy, sx);
            if (sameGroup && std::fabs(Wrap(offset - a_ring.offset)) < kKeepOffset) {
                offset = a_ring.offset;
            }
            for (std::size_t k = 0; k < m.size(); ++k) {
                m[k].sector = Wrap(offset + step * static_cast<float>(k));
            }
            a_ring.offset = offset;
            a_ring.roster = std::move(roster);
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
            ring.members.push_back({id, bearing, bearing, now});
        } else {
            member->bearing = bearing;
            member->seen = now;
        }
        std::erase_if(ring.members, [&](const Member &a_m) {
            return std::chrono::duration<float>(now - a_m.seen).count() > kStale;
        });
        if (std::chrono::duration<float>(now - ring.solvedAt).count() >= kSolveEvery) {
            ring.solvedAt = now;
            if (ring.members.size() >= 2) {
                Solve(ring);
            } else {
                ring.solved = false;
                ring.roster.clear();
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
        }
        const auto center = a_state.target.position;
        const float bearing = Bearing(center, a_state.self.position);
        const float off = Wrap(sector - bearing);
        const float dist = a_state.target.distance;
        const float reach = a_state.weaponReach > 0.0f ? a_state.weaponReach : 150.0f;
        // Approaching: walk the arc a bit inside the current distance until in its sector, then straight in. Close
        // to the target (1.5 x reach) it just fights: no walking around under its weapon.
        // Waiting: walk to its sector at the wait ring; once there, circle as before.
        float radius = 0.0f;
        if (approaching && std::fabs(off) > kApproachTolerance && dist > reach * 1.5f) {
            radius = std::clamp(dist * 0.85f, reach + 60.0f, dist);
        } else if (waiting && std::fabs(off) > kWaitTolerance) {
            radius = a_decision.holdDistance;
        }
        const int mode = radius <= 0.0f ? 0 : (waiting ? 2 : 1);
        if (Config::GetInstance().IsDebugTarget(id)) {
            auto &told = g_told[id];
            const auto now = Clock::now();
            if (told.mode != mode || std::fabs(Wrap(told.sector - sector)) > 5.0f * kDeg ||
                now - told.at > std::chrono::seconds(3)) {
                told = {mode, sector, now};
                constexpr const char *kMode[] = {"in its sector", "approaching around the target", "waiting: moving to its sector"};
                LOG_DEBUG("Ring 0x{:08X}: sector {:.0f} of {} (now at {:.0f}, {:+.0f} off, dist {:.0f}) - {}{}",
                          static_cast<std::uint32_t>(id), sector / kDeg, count, bearing / kDeg, off / kDeg, dist, kMode[mode],
                          radius > 0.0f ? std::format(", arc goal at {:.0f}", radius) : std::string());
            }
        }
        if (radius <= 0.0f) {
            g_goals.Erase(id);
            return;
        }
        const float legAngle = bearing + std::clamp(off, -kArcStep, kArcStep);
        const RE::NiPoint3 goal(center.x + radius * std::cos(legAngle), center.y + radius * std::sin(legAngle), center.z);
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
