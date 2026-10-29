#include "TargetDanger.h"
#include "ActorStateData.h"
#include "ActorUtils.h"
#include "Boss.h"
#include "Config.h"
#include "Logger.h"
#include "ThreadSafeMap.h"

#include <algorithm>
#include <array>
#include <chrono>

namespace CombatAI::TargetDanger
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        struct Cached
        {
            RE::FormID target = 0;
            Clock::time_point at{};
            Danger danger{};
        };
        ThreadSafeMap<RE::FormID, Cached> g_state;

        struct RaceID
        {
            RE::FormID local;
            const char *plugin;
        };
        // Race FormIDs read from the masters (RACE records, 2026-10-29). GiantRace carries no ActorTypeGiant keyword,
        // werewolves none of their own, so races go by FormID.
        constexpr std::array kHugeRaces{
            RaceID{0x0131F9, "Skyrim.esm"},   // GiantRace
            RaceID{0x0CAE13, "Skyrim.esm"},   // C00GiantOutsideWhiterunRace
            RaceID{0x0131FF, "Skyrim.esm"},   // MammothRace
            RaceID{0x0131F1, "Skyrim.esm"},   // DwarvenCenturionRace
            RaceID{0x01CAD8, "Dragonborn.esm"}, // DLC2GhostFrostGiantRace
            RaceID{0x014495, "Dragonborn.esm"}, // DLC2LurkerRace
        };
        constexpr std::array kBeastRaces{
            RaceID{0x0CDD84, "Skyrim.esm"},     // WerewolfBeastRace
            RaceID{0x01E17B, "Dragonborn.esm"}, // DLC2WerebearBeastRace
            RaceID{0x00283A, "Dawnguard.esm"},  // DLC1VampireBeastRace
        };

        template <std::size_t N>
        std::array<RE::FormID, N> Resolve(const std::array<RaceID, N> &a_ids)
        {
            std::array<RE::FormID, N> out{};
            auto *data = RE::TESDataHandler::GetSingleton();
            for (std::size_t i = 0; i < N; ++i) {
                out[i] = data ? data->LookupFormID(a_ids[i].local, a_ids[i].plugin) : 0; // 0 when the plugin is absent
            }
            return out;
        }

        template <std::size_t N>
        bool RaceIn(const RE::TESRace *a_race, const std::array<RE::FormID, N> &a_ids)
        {
            return a_race && std::ranges::find(a_ids, a_race->GetFormID()) != a_ids.end();
        }

        // 0 below 1.25x the NPC's own value, 1 from 2.75x
        constexpr float Above(float a_target, float a_self)
        {
            return std::clamp((a_target / (std::max)(a_self, 1.0f) - 1.25f) / 1.5f, 0.0f, 1.0f);
        }
        static_assert(Above(10.0f, 10.0f) == 0.0f && Above(20.0f, 10.0f) == 0.5f && Above(50.0f, 10.0f) == 1.0f &&
                      Above(5.0f, 0.0f) == 1.0f);

        Danger Rate(RE::Actor *a_self, RE::Actor *a_target, bool a_debug)
        {
            static const auto huge = Resolve(kHugeRaces);
            static const auto beast = Resolve(kBeastRaces);
            const auto &matrix = Config::GetInstance().GetDecisionMatrix();

            Danger d;
            const auto *race = a_target->GetRace();
            d.huge = RaceIn(race, huge) || ActorUtils::SafeHasKeywordString(a_target, "ActorTypeDragon");
            d.beast = RaceIn(race, beast);
            const bool boss = Boss::Get(a_target).boss;

            auto *selfAV = ActorUtils::SafeAsActorValueOwner(a_self);
            auto *targetAV = ActorUtils::SafeAsActorValueOwner(a_target);
            const float level = Above(static_cast<float>(a_target->GetLevel()), static_cast<float>(a_self->GetLevel()));
            const float health = selfAV && targetAV ? Above(targetAV->GetPermanentActorValue(RE::ActorValue::kHealth),
                                                            selfAV->GetPermanentActorValue(RE::ActorValue::kHealth))
                                                    : 0.0f;
            const float strength = (std::max)(level, health) * matrix.targetDangerStrengthWeight;

            float safe = 1.0f - strength;
            safe *= d.huge ? 0.4f : 1.0f;
            safe *= d.beast ? 0.5f : 1.0f;
            safe *= boss ? 0.7f : 1.0f;
            d.score = std::clamp(1.0f - safe, 0.0f, 1.0f);
            if (a_debug) {
                LOG_DEBUG("Danger 0x{:08X}: target 0x{:08X} {} -> {:.2f} (level {} vs {}, health {:.0f} vs {:.0f}, strength {:.2f}{}{}{})",
                          a_self->GetFormID(), a_target->GetFormID(), a_target->GetName(), d.score, a_target->GetLevel(),
                          a_self->GetLevel(), targetAV ? targetAV->GetPermanentActorValue(RE::ActorValue::kHealth) : 0.0f,
                          selfAV ? selfAV->GetPermanentActorValue(RE::ActorValue::kHealth) : 0.0f, strength,
                          d.huge ? ", huge" : "", d.beast ? ", beast" : "", boss ? ", boss" : "");
            }
            return d;
        }
    } // namespace

    void Update(RE::Actor *a_actor, ActorStateData &a_state, bool a_debug)
    {
        const RE::FormID id = a_actor->GetFormID();
        auto *target = a_state.target.isValid && a_state.target.targetFormID
                           ? RE::TESForm::LookupByID<RE::Actor>(a_state.target.targetFormID)
                           : nullptr;
        if (!Config::GetInstance().GetDecisionMatrix().enableTargetDanger || !target) {
            g_state.Erase(id);
            return;
        }
        const auto now = Clock::now();
        Cached c = g_state.Find(id).value_or(Cached{});
        if (c.target != target->GetFormID() || now - c.at >= std::chrono::seconds(5)) {
            const bool changed = c.target != target->GetFormID();
            c = {target->GetFormID(), now, Rate(a_actor, target, a_debug && changed)};
            g_state.ModifyOrCreate(id, [&](Cached &a_entry) { a_entry = c; });
        }
        // A giant counts as a crowd: the threat level otherwise counts enemies only
        auto &threat = a_state.combatContext.threatLevel;
        if (c.danger.score >= 0.7f) {
            threat = (std::max)(threat, ThreatLevel::High);
        } else if (c.danger.score >= 0.4f) {
            threat = (std::max)(threat, ThreatLevel::Moderate);
        }
    }

    Danger Of(RE::Actor *a_actor)
    {
        const auto c = a_actor ? g_state.Find(a_actor->GetFormID()) : std::nullopt;
        return c ? c->danger : Danger{};
    }

    void Evict(RE::FormID a_formID)
    {
        g_state.Erase(a_formID);
    }
} // namespace CombatAI::TargetDanger
