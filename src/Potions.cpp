#include "Potions.h"
#include "ActorStateData.h"
#include "ActorUtils.h"
#include "Config.h"
#include "Intents.h"
#include "Logger.h"
#include "ThreadSafeMap.h"

#include <algorithm>
#include <chrono>

namespace CombatAI::Potions
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        struct State
        {
            Clock::time_point seen{};      // last brain tick: vanilla's healing-potion pick is off while recent
            Clock::time_point drank{};     // last drink we started (cooldown)
            Clock::time_point scanned{};   // last inventory scan
            RE::AlchemyItem *potion = nullptr; // best healing potion at that scan
        };
        ThreadSafeMap<RE::FormID, State> g_state;
        bool g_otherMod = false; // NPCs Use Potions is loaded

        // How much health a potion restores: its largest Restore Health magnitude. 0 for a potion with any harmful
        // effect, and for Fortify Health (Peak Value Modifier) and regeneration (Heal Rate), which don't heal now.
        float HealthRestore(const RE::MagicItem *a_magic)
        {
            if (!a_magic) {
                return 0.0f;
            }
            float best = 0.0f;
            for (const auto *effect : a_magic->effects) {
                const auto *base = effect ? effect->baseEffect : nullptr;
                if (!base) {
                    continue;
                }
                if (base->IsDetrimental()) {
                    return 0.0f;
                }
                if (base->GetArchetype() == RE::EffectSetting::Archetype::kValueModifier &&
                    base->data.primaryAV == RE::ActorValue::kHealth &&
                    base->data.flags.none(RE::EffectSetting::EffectSettingData::Flag::kRecover)) {
                    best = (std::max)(best, effect->GetMagnitude());
                }
            }
            return best;
        }

        // The strongest healing potion it carries (shortcut: strongest, not the smallest one that heals enough)
        RE::AlchemyItem *BestPotion(RE::Actor *a_actor)
        {
            RE::AlchemyItem *best = nullptr;
            float bestHeal = 0.0f;
            const auto items = a_actor->GetInventory([](RE::TESBoundObject &a_object) { return a_object.Is(RE::FormType::AlchemyItem); });
            for (const auto &[object, data] : items) {
                auto *potion = object->As<RE::AlchemyItem>();
                if (!potion || data.first <= 0 || potion->IsFood() || potion->IsPoison()) {
                    continue;
                }
                if (const float heal = HealthRestore(potion); heal > bestHeal) {
                    bestHeal = heal;
                    best = potion;
                }
            }
            return best;
        }

        bool Enabled()
        {
            return Config::GetInstance().GetDecisionMatrix().enableNPCPotions && !g_otherMod;
        }

        // Slot 0x0F of CombatInventoryItemMagicT<CombatInventoryItemPotion, CombatMagicCasterRestore> (SE 265372 /
        // AE 211590): SE 44776 / AE 46277 = base check && the Restore caster's check; false keeps the item unequipped
        struct PotionShouldEquip
        {
            static bool thunk(RE::CombatInventoryItemMagic *a_item, RE::CombatController *a_controller)
            {
                if (Enabled() && a_controller) {
                    const auto actor = a_controller->attackerHandle.get();
                    const auto s = actor ? g_state.Find(actor->GetFormID()) : std::nullopt;
                    if (s && Clock::now() - s->seen < std::chrono::seconds(2) && HealthRestore(a_item->GetMagic()) > 0.0f) {
                        return false;
                    }
                }
                return func(a_item, a_controller);
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };
    } // namespace

    void Install()
    {
        g_otherMod = GetModuleHandleW(L"NPCsUsePotions.dll") != nullptr;
        REL::Relocation<std::uintptr_t> vtable{RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemPotion_CombatMagicCasterRestore_[0]};
        PotionShouldEquip::func = vtable.write_vfunc(0x0F, PotionShouldEquip::thunk);
        if (g_otherMod) {
            LOG_INFO("Potions: NPCs Use Potions is loaded - it runs NPC potions, ours stay off");
        } else {
            LOG_INFO("Potions: vanilla healing-potion pick off for brain NPCs (they back off, then drink)");
        }
    }

    void Update(RE::Actor *a_actor, const ActorStateData &a_state, bool a_debug)
    {
        if (!ActorUtils::SafeHasKeywordString(a_actor, "ActorTypeNPC")) {
            return; // creatures keep vanilla (no drink animations for them)
        }
        const auto now = Clock::now();
        const RE::FormID id = a_actor->GetFormID();
        State s = g_state.Find(id).value_or(State{});
        s.seen = now;
        const auto &matrix = Config::GetInstance().GetDecisionMatrix();
        const auto cooldown = std::chrono::duration<float>(matrix.potionCooldownSeconds);
        const bool want = Enabled() && a_state.self.healthPercent <= matrix.potionHealthThreshold && now - s.drank >= cooldown;
        if (want && now - s.scanned >= std::chrono::seconds(1)) {
            s.potion = BestPotion(a_actor);
            s.scanned = now;
        }
        if (!want || !s.potion) {
            Intents::EndDrinkWait(a_actor);
            g_state.ModifyOrCreate(id, [&](State &a_entry) { a_entry = s; });
            return;
        }

        const bool room = !a_state.target.isValid || a_state.target.distance >= matrix.safeDrinkDistance;
        const float waited = room ? 0.0f : Intents::PostDrinkWait(a_actor);
        if (!room && waited < matrix.safeDrinkMaxWaitSeconds) {
            if (a_debug) {
                LOG_DEBUG("Potion 0x{:08X}: hp {:.0f}%, backs off to drink {} (target at {:.0f}, {:.1f} s)", id,
                          a_state.self.healthPercent * 100.0f, s.potion->GetName(), a_state.target.distance, waited);
            }
            g_state.ModifyOrCreate(id, [&](State &a_entry) { a_entry = s; });
            return;
        }

        Intents::EndDrinkWait(a_actor);
        s.drank = now;
        s.scanned = {}; // rescan next time: this one may have been the last
        g_state.ModifyOrCreate(id, [&](State &a_entry) { a_entry = s; });
        if (a_debug) {
            LOG_DEBUG("Potion 0x{:08X}: hp {:.0f}%, drinks {} ({})", id, a_state.self.healthPercent * 100.0f,
                      s.potion->GetName(),
                      room ? std::format("target at {:.0f}", a_state.target.distance)
                           : std::format("target still at {:.0f} after {:.1f} s", a_state.target.distance, waited));
        }
        // Equip on the main thread, like NPCs Use Potions: the potion is drunk and animation mods see it
        SKSE::GetTaskInterface()->AddTask([handle = a_actor->GetHandle(), potion = s.potion]() {
            const auto actor = handle.get();
            if (!actor || actor->IsDead()) {
                return;
            }
            const auto items = actor->GetInventory([potion](RE::TESBoundObject &a_object) { return &a_object == potion; });
            if (const auto it = items.find(potion); it == items.end() || it->second.first <= 0) {
                return; // drunk or dropped since the scan
            }
            RE::ActorEquipManager::GetSingleton()->EquipObject(actor.get(), potion, nullptr, 1, nullptr, true, false, false);
        });
    }

    void Evict(RE::FormID a_formID)
    {
        g_state.Erase(a_formID);
    }
} // namespace CombatAI::Potions
