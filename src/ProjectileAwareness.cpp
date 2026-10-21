#include "ProjectileAwareness.h"
#include "Config.h"
#include "pch.h"
#include <random>

namespace CombatAI
{
    void ProjectileAwareness::Update()
    {
        std::vector<Threat> threats;
        if (Config::GetInstance().GetProjectileAwareness().enableProjectileDodge) {
            auto *manager = RE::CombatManager::GetSingleton();
            auto *threatMap = manager ? manager->combatThreatMap : nullptr;
            if (threatMap) {
                RE::BSReadLockGuard lock(threatMap->lock);
                for (const auto &entry : threatMap->threats) {
                    const auto &threat = entry.second;
                    if (!threat) {
                        continue;
                    }
                    RE::NiPointer<RE::TESObjectREFR> shooter;
                    RE::TESObjectREFR::LookupByHandle(threat->shooter, shooter);
                    threats.push_back({threat->formid, shooter ? shooter->GetFormID() : 0, threat->pos,
                                       threat->velocity, threat->rad});
                }
            }
        }
        std::lock_guard guard(m_lock);
        m_threats = std::move(threats);
    }

    IncomingProjectile ProjectileAwareness::Assess(RE::FormID a_self, const RE::NiPoint3 &a_selfPos,
                                                   float a_selfRadius) const
    {
        const auto &settings = Config::GetInstance().GetProjectileAwareness();
        IncomingProjectile best;
        std::lock_guard guard(m_lock);
        for (const auto &threat : m_threats) {
            if (threat.shooter == a_self) {
                continue;
            }
            // Horizontal closest approach along the flight path. Projectiles fly at chest
            // height while positions are at the feet, so skip ones far above/below instead.
            if (std::abs(threat.pos.z - a_selfPos.z) > 400.0f) {
                continue;
            }
            const RE::NiPoint3 rel(a_selfPos.x - threat.pos.x, a_selfPos.y - threat.pos.y, 0.0f);
            const RE::NiPoint3 vel(threat.velocity.x, threat.velocity.y, 0.0f);
            const float speedSq = vel.SqrLength();
            if (speedSq < 50.0f * 50.0f) {
                continue; // runes and other stationary threats: not handled yet
            }
            const float t = rel.Dot(vel) / speedSq;
            if (t <= 0.0f || (best.valid && t >= best.timeToImpact)) {
                continue; // moving away, or a later hit than one already found
            }
            const RE::NiPoint3 miss = rel - vel * t;
            if (miss.Length() > a_selfRadius + threat.radius + settings.hitMargin) {
                continue; // will pass by
            }
            best.valid = true;
            best.timeToImpact = t;
            best.speed = std::sqrt(speedSq);
            best.flightDir = vel / best.speed;
            best.missOffset = miss;
            best.threatID = threat.id;
            best.shooterID = threat.shooter;
        }
        return best;
    }

    bool ProjectileAwareness::RollReaction(RE::FormID a_actor, RE::FormID a_threat, float a_chance)
    {
        thread_local std::mt19937 gen{std::random_device{}()};
        bool react = false;
        m_rolls.ModifyOrCreate(a_actor, [&](Roll &roll) {
            if (roll.threat != a_threat) {
                roll.threat = a_threat;
                roll.react = std::uniform_real_distribution<float>(0.0f, 1.0f)(gen) < a_chance;
            }
            react = roll.react;
        });
        return react;
    }
} // namespace CombatAI
