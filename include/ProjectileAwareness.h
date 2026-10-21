#pragma once

#include "ActorStateData.h"
#include "ThreadSafeMap.h"
#include "pch.h"
#include <mutex>
#include <vector>

namespace CombatAI
{
    // Reads the engine's combat threat list (CombatManager+0x28, layout verified in AE):
    // projectiles and flame streams with position, velocity and shooter. Snapshotted once per
    // frame on the main thread; each actor's update checks the snapshot for a hit on course.
    class ProjectileAwareness
    {
      public:
        static ProjectileAwareness &GetInstance()
        {
            static ProjectileAwareness instance;
            return instance;
        }

        ProjectileAwareness(const ProjectileAwareness &) = delete;
        ProjectileAwareness &operator=(const ProjectileAwareness &) = delete;

        // Main thread, once per frame
        void Update();

        // Soonest threat on course to hit this actor (closest approach within its radius)
        IncomingProjectile Assess(RE::FormID a_self, const RE::NiPoint3 &a_selfPos, float a_selfRadius) const;

        // One reaction roll per actor per projectile, so the chance isn't re-rolled every tick
        bool RollReaction(RE::FormID a_actor, RE::FormID a_threat, float a_chance);

        void Evict(RE::FormID a_actor) { m_rolls.Erase(a_actor); }

      private:
        ProjectileAwareness() = default;
        ~ProjectileAwareness() = default;

        struct Threat
        {
            RE::FormID id = 0;
            RE::FormID shooter = 0;
            RE::NiPoint3 pos;
            RE::NiPoint3 velocity;
            float radius = 0.0f;
        };

        struct Roll
        {
            RE::FormID threat = 0;
            bool react = false;
        };

        mutable std::mutex m_lock;
        std::vector<Threat> m_threats;
        ThreadSafeMap<RE::FormID, Roll> m_rolls;
    };
} // namespace CombatAI
