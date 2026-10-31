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
                                       float a_windowMax, float a_restSeconds)
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
        if (const auto rested = m_rested.Find(a_attacker); rested && now - *rested < a_restSeconds) {
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
                    LOG_DEBUG("Slots 0x{:08X}: 0x{:08X} drops its turn (dead, out of combat or another target)",
                              static_cast<std::uint32_t>(a_target), static_cast<std::uint32_t>(it->first));
                    it = bucket.erase(it);
                } else if (now - it->second.acquireTime > it->second.window) {
                    m_rested.ModifyOrCreate(it->first, [&](float &a_time) { a_time = now; });
                    LOG_DEBUG("Slots 0x{:08X}: turn of 0x{:08X} ran out ({:.1f} s)", static_cast<std::uint32_t>(a_target),
                              static_cast<std::uint32_t>(it->first), it->second.window);
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
                const auto &slot = bucket.emplace(a_attacker, SlotInfo{now, RandRange(a_windowMin, a_windowMax)}).first->second;
                granted = true;
                // Every turn change on a target (1.15.1): proves what freed a slot when a waiter flickers
                LOG_DEBUG("Slots 0x{:08X}: 0x{:08X} takes a turn ({:.1f} s), {} of {} taken",
                          static_cast<std::uint32_t>(a_target), static_cast<std::uint32_t>(a_attacker), slot.window,
                          bucket.size(), a_maxSlots);
            }
        });

        return granted;
    }

    bool AttackCoordinator::SlotsFull(RE::FormID a_target, RE::FormID a_attacker, int a_maxSlots) const
    {
        const float now = m_now.load();
        bool full = false;
        m_slots.WithReadLock([&](const auto &a_map) {
            const auto it = a_map.find(a_target);
            if (it == a_map.end() || it->second.contains(a_attacker)) {
                return;
            }
            int live = 0;
            for (const auto &[id, slot] : it->second) {
                live += now - slot.acquireTime <= slot.window ? 1 : 0;
            }
            full = live >= a_maxSlots;
        });
        return full;
    }

    bool AttackCoordinator::HasSlot(RE::FormID a_target, RE::FormID a_attacker) const
    {
        const float now = m_now.load();
        bool has = false;
        m_slots.WithReadLock([&](const auto &a_map) {
            const auto it = a_map.find(a_target);
            if (it == a_map.end()) {
                return;
            }
            const auto slot = it->second.find(a_attacker);
            has = slot != it->second.end() && now - slot->second.acquireTime <= slot->second.window;
        });
        return has;
    }

    void AttackCoordinator::Release(RE::FormID a_target, RE::FormID a_attacker, const char *a_why)
    {
        if (a_target == 0 || a_attacker == 0) {
            return;
        }
        bool released = false;
        m_slots.Modify(a_target, [&](SlotBucket &bucket) { released = bucket.erase(a_attacker) > 0; });
        if (released) {
            LOG_DEBUG("Slots 0x{:08X}: 0x{:08X} gives up its turn ({})", static_cast<std::uint32_t>(a_target),
                      static_cast<std::uint32_t>(a_attacker), a_why);
        }
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
