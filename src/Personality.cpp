#include "Personality.h"
#include "Boss.h"
#include "CombatStyleEnhancer.h"
#include "Config.h"
#include "Logger.h"
#include "ThreadSafeMap.h"

#include <algorithm>

namespace CombatAI::Personality
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        struct Cached
        {
            Traits traits;
            Clock::time_point at{};
        };
        ThreadSafeMap<RE::FormID, Cached> g_cache;

        // Fixed per-NPC offset in [-0.15, 0.15] (run 52: one template's bandits read the same data, the offset is the spread)
        float Jitter(RE::FormID a_formID, std::uint32_t a_salt)
        {
            std::uint32_t h = (a_formID ^ a_salt) * 0x9E3779B1u;
            h ^= h >> 15;
            return (static_cast<float>(h & 0xFFFF) / 65535.0f - 0.5f) * 0.3f;
        }

        Traits Compute(RE::Actor *a_actor)
        {
            auto *av = a_actor->AsActorValueOwner();
            const float confidence = std::clamp(av->GetActorValue(RE::ActorValue::kConfidence), 0.0f, 4.0f) / 4.0f;
            const float aggression = std::clamp(av->GetActorValue(RE::ActorValue::kAggression), 0.0f, 3.0f) / 3.0f;
            float offense = 0.5f, avoid = 0.3f;
            if (auto *style = CombatStyleEnhancer::GetCombatStyle(a_actor)) {
                const float off = (std::max)(style->generalData.offensiveMult, 0.0f);
                const float def = (std::max)(style->generalData.defensiveMult, 0.0f);
                offense = off + def > 0.0f ? off / (off + def) : 0.5f;
                avoid = std::clamp(style->generalData.avoidThreatChance, 0.0f, 1.0f);
            }
            const RE::FormID id = a_actor->GetFormID();
            Traits t;
            // The actor values weigh more than the style's balance, which is all-offense for many vanilla styles
            t.risk = std::clamp(0.45f * confidence + 0.35f * aggression + 0.2f * offense + Jitter(id, 1), 0.0f, 1.0f);
            t.caution = std::clamp(0.45f * (1.0f - confidence) + 0.2f * (1.0f - offense) + 0.35f * avoid + Jitter(id, 2), 0.0f, 1.0f);
            t.patience = std::clamp(0.7f * (1.0f - aggression) + 0.3f * (1.0f - offense) + Jitter(id, 3), 0.0f, 1.0f);
            if (Config::GetInstance().IsDebugTarget(id)) {
                LOG_DEBUG("Personality 0x{:08X}: confidence {:.0f}, aggression {:.0f}, style offense {:.2f}, avoid threat {:.2f} -> risk {:.2f}, caution {:.2f}, patience {:.2f}",
                          id, confidence * 4.0f, aggression * 3.0f, offense, avoid, t.risk, t.caution, t.patience);
            }
            return t;
        }
    } // namespace

    Traits Get(RE::Actor *a_actor)
    {
        if (!a_actor || !Config::GetInstance().GetDecisionMatrix().enablePersonality) {
            return {};
        }
        const auto now = Clock::now();
        const RE::FormID id = a_actor->GetFormID();
        if (const auto c = g_cache.Find(id); c && now - c->at < std::chrono::seconds(10)) {
            return c->traits;
        }
        const Traits t = Compute(a_actor);
        g_cache.ModifyOrCreate(id, [&](Cached &c) { c = {t, now}; });
        return t;
    }

    Traits Effective(RE::Actor *a_actor)
    {
        Traits t = Get(a_actor);
        switch (Boss::CurrentPhase(a_actor)) {
        case Boss::Phase::None:
            return t;
        case Boss::Phase::Enraged:
            t.risk = (std::min)(1.0f, t.risk + 0.2f);
            break;
        case Boss::Phase::Desperate:
            if (Boss::Brave(a_actor)) {
                t.risk = 1.0f; // all-in
                t.caution = 0.25f;
                return t;
            }
            t.risk *= 0.5f; // falls back among its minions (EvaluateBoss)
            t.caution = 0.9f;
            return t;
        default:
            break;
        }
        t.caution = (std::max)(t.caution, 0.4f); // a boss still defends well (run 55's chief read caution 0.01)
        return t;
    }
} // namespace CombatAI::Personality
