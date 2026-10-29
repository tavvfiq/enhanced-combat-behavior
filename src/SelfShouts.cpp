#include "SelfShouts.h"
#include "ActorStateData.h"
#include "Config.h"
#include "Logger.h"
#include "ThreadSafeMap.h"

#include <algorithm>
#include <array>
#include <chrono>

namespace CombatAI::SelfShouts
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        enum Kind : std::size_t { kEthereal, kSlowTime, kAttackBuff, kKindCount };
        constexpr std::array<const char *, kKindCount> kKindName{"ethereal", "slow time", "attack speed"};

        struct Known
        {
            RE::TESShout *shout = nullptr;
            std::int32_t level = 0; // highest variation with a word and a spell (SE 22902 / AE 23359 does the same)
        };

        struct State
        {
            Clock::time_point tried{};   // last start we asked for (retry gate)
            Clock::time_point scanned{}; // last spell-list scan
            std::array<Known, kKindCount> known{};
        };
        ThreadSafeMap<RE::FormID, State> g_state;

        // SE 37850 / AE 38804 (same body in 1.6.1170 and 1.7.104): bool(actor, shout, level, target). Fails if the
        // process shout slot is busy (SE 38513 / AE 39523); else RequestCast of variations[level].spell on the voice
        // caster (GetMagicCaster(kOther)). Called by CastShout::Enter's helper SE 48803 / AE 49758 (C).
        bool StartShout(RE::Actor *a_actor, RE::TESShout *a_shout, std::int32_t a_level, RE::TESObjectREFR *a_target)
        {
            using func_t = decltype(&StartShout);
            static REL::Relocation<func_t> func{RELOCATION_ID(37850, 38804)};
            return func(a_actor, a_shout, a_level, a_target);
        }

        // What a shout's spell does for its caster: kKindCount for anything else
        Kind Classify(const RE::SpellItem *a_spell)
        {
            if (!a_spell) {
                return kKindCount;
            }
            for (const auto *effect : a_spell->effects) {
                const auto *base = effect ? effect->baseEffect : nullptr;
                if (!base) {
                    continue;
                }
                switch (base->GetArchetype()) {
                case RE::EffectSetting::Archetype::kEtherealize:
                    return kEthereal;
                case RE::EffectSetting::Archetype::kSlowTime:
                    return kSlowTime;
                case RE::EffectSetting::Archetype::kValueModifier:
                case RE::EffectSetting::Archetype::kPeakValueModifier:
                    if (!base->IsDetrimental() && base->data.primaryAV == RE::ActorValue::kWeaponSpeedMult) {
                        return kAttackBuff;
                    }
                    break;
                default:
                    break;
                }
            }
            return kKindCount;
        }

        std::array<Known, kKindCount> KnownShouts(RE::Actor *a_actor)
        {
            std::array<Known, kKindCount> known{};
            auto *npc = a_actor->GetActorBase();
            const auto *list = npc ? npc->GetSpellList() : nullptr;
            if (!list || !list->shouts) {
                return known;
            }
            for (std::uint32_t i = 0; i < list->numShouts; ++i) {
                auto *shout = list->shouts[i];
                if (!shout) {
                    continue;
                }
                for (std::int32_t level = 2; level >= 0; --level) {
                    const auto &variation = shout->variations[level];
                    if (variation.word && variation.spell) {
                        if (const Kind kind = Classify(variation.spell); kind != kKindCount && !known[kind].shout) {
                            known[kind] = {shout, level};
                        }
                        break;
                    }
                }
            }
            return known;
        }

        // One of the shout's effects is already on the actor
        bool Active(RE::Actor *a_actor, const Known &a_known)
        {
            auto *target = a_actor->AsMagicTarget();
            const auto *spell = a_known.shout->variations[a_known.level].spell;
            return target && std::ranges::any_of(spell->effects, [target](const RE::Effect *a_effect) {
                       return a_effect && a_effect->baseEffect && target->HasMagicEffect(a_effect->baseEffect);
                   });
        }
    } // namespace

    void Update(RE::Actor *a_actor, const ActorStateData &a_state, bool a_debug)
    {
        const auto &matrix = Config::GetInstance().GetDecisionMatrix();
        if (!matrix.enableSelfShouts || a_actor->IsDead() || a_state.self.healthPercent <= 0.0f) {
            return;
        }
        const auto now = Clock::now();
        const RE::FormID id = a_actor->GetFormID();
        State s = g_state.Find(id).value_or(State{});
        if (now - s.scanned >= std::chrono::seconds(10)) {
            s.known = KnownShouts(a_actor);
            s.scanned = now;
        }
        const bool knowsAny = std::ranges::any_of(s.known, [](const Known &a_known) { return a_known.shout != nullptr; });
        if (!knowsAny || now - s.tried < std::chrono::seconds(2) || a_actor->GetVoiceRecoveryTime() > 0.0f) {
            g_state.ModifyOrCreate(id, [&](State &a_entry) { a_entry = s; });
            return;
        }

        const float hp = a_state.self.healthPercent;
        const std::array<bool, kKindCount> want{
            hp <= matrix.potionHealthThreshold, // untouchable while it backs off and drinks
            matrix.selfShoutSlowTime && (hp <= matrix.healthThreshold || a_state.combatContext.threatLevel >= ThreatLevel::High),
            a_state.self.isMelee && a_state.target.isValid && a_state.target.distance <= 800.0f, // engaging
        };
        Kind kind = kKindCount;
        for (std::size_t k = 0; k < kKindCount && kind == kKindCount; ++k) {
            if (want[k] && s.known[k].shout && !Active(a_actor, s.known[k])) {
                kind = static_cast<Kind>(k);
            }
        }
        if (kind == kKindCount) {
            g_state.ModifyOrCreate(id, [&](State &a_entry) { a_entry = s; });
            return;
        }
        s.tried = now;
        g_state.ModifyOrCreate(id, [&](State &a_entry) { a_entry = s; });

        const Known known = s.known[kind];
        SKSE::GetTaskInterface()->AddTask([handle = a_actor->GetHandle(), known, kind, a_debug, targetID = a_state.target.targetFormID]() {
            const auto actor = handle.get();
            if (!actor || actor->IsDead() || actor->GetVoiceRecoveryTime() > 0.0f) {
                return;
            }
            auto *target = targetID ? RE::TESForm::LookupByID<RE::Actor>(targetID) : nullptr;
            const bool started = StartShout(actor.get(), known.shout, known.level, target);
            if (a_debug) {
                LOG_DEBUG("SelfShout 0x{:08X}: {} {} ({}, level {})", actor->GetFormID(), started ? "shouts" : "could not shout",
                          known.shout->GetName(), kKindName[kind], known.level + 1);
            }
        });
    }

    void Evict(RE::FormID a_formID)
    {
        g_state.Erase(a_formID);
    }
} // namespace CombatAI::SelfShouts
