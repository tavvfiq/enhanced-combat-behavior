#include "AttackCoordinator.h"
#include "ActorUtils.h"
#include "Logger.h"

#include <random>

namespace CombatAI
{
    namespace
    {
        // Thread-local RNG for slot window jitter (cheap, no shared state).
        float RandRange(float a_min, float a_max)
        {
            if (a_max <= a_min) {
                return a_min;
            }
            static thread_local std::mt19937 gen{std::random_device{}()};
            std::uniform_real_distribution<float> dist(a_min, a_max);
            return dist(gen);
        }
    } // namespace

    bool AttackCoordinator::TryAcquire(RE::FormID a_target, RE::FormID a_attacker, int a_maxSlots, float a_windowMin,
                                       float a_windowMax)
    {
        if (a_target == 0 || a_attacker == 0) {
            return true; // Can't pace without valid ids - allow the attack
        }
        if (a_maxSlots <= 0) {
            return false; // Pacing set to block all attackers
        }

        const float now = m_now.load();
        bool granted = false;

        // Rotation: a holder whose slot just ran out sits out briefly while someone else is waiting. Without this
        // the holder, already in reach and asking every tick, usually took the freed slot straight back.
        constexpr float kRestSeconds = 1.5f;
        if (const auto rested = m_rested.Find(a_attacker); rested && now - *rested < kRestSeconds) {
            bool othersWaiting = false;
            m_held.WithReadLock([&](const auto &held) {
                for (const auto &[id, since] : held) {
                    othersWaiting |= id != a_attacker && now - since < 1.0f;
                }
            });
            if (othersWaiting) {
                return false;
            }
        }

        m_slots.ModifyOrCreate(a_target, [&](SlotBucket &bucket) {
            // Rotate out expired slots so waiting attackers get their turn.
            for (auto it = bucket.begin(); it != bucket.end();) {
                // A holder that died, left combat or turned on someone else frees its slot now: with a long window (the
                // Duel preset) it would otherwise keep the others waiting until the window ran out
                auto *holder = RE::TESForm::LookupByID<RE::Actor>(it->first);
                auto *controller = holder ? ActorUtils::SafeGetCombatController(holder) : nullptr;
                const auto holderTarget = controller ? controller->targetHandle.get() : nullptr; // the brain's target
                if (!holder || holder->IsDead() || !holder->IsInCombat() || !holderTarget ||
                    holderTarget->GetFormID() != a_target) {
                    it = bucket.erase(it);
                } else if (now - it->second.acquireTime > it->second.window) {
                    m_rested.ModifyOrCreate(it->first, [&](float &a_time) { a_time = now; });
                    it = bucket.erase(it);
                } else {
                    ++it;
                }
            }

            auto existing = bucket.find(a_attacker);
            if (existing != bucket.end()) {
                // Still within its window - keep attacking. Do NOT refresh acquireTime,
                // otherwise a holder that keeps attacking would never rotate out.
                granted = true;
                return;
            }

            if (static_cast<int>(bucket.size()) < a_maxSlots) {
                bucket.emplace(a_attacker, SlotInfo{now, RandRange(a_windowMin, a_windowMax)});
                granted = true;
            }
        });

        return granted;
    }

    void AttackCoordinator::Release(RE::FormID a_target, RE::FormID a_attacker)
    {
        if (a_target == 0 || a_attacker == 0) {
            return;
        }
        m_slots.Modify(a_target, [&](SlotBucket &bucket) { bucket.erase(a_attacker); });
    }

    void AttackCoordinator::Cleanup()
    {
        // Drop rests long over, and empty buckets, so the maps don't grow unbounded across encounters.
        const float now = m_now.load();
        m_rested.WithWriteLock([&](auto &map) { std::erase_if(map, [&](const auto &e) { return now - e.second > 10.0f; }); });
        m_slots.WithWriteLock([](std::unordered_map<RE::FormID, SlotBucket> &map) {
            for (auto it = map.begin(); it != map.end();) {
                if (it->second.empty()) {
                    it = map.erase(it);
                } else {
                    ++it;
                }
            }
        });
    }

    void AttackCoordinator::Reset()
    {
        m_slots.Clear();
        m_rested.Clear();
    }
} // namespace CombatAI
