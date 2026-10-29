#include "Potions.h"
#include "ActorStateData.h"
#include "ActorUtils.h"
#include "Boss.h"
#include "Config.h"
#include "Intents.h"
#include "Logger.h"
#include "ThreadSafeMap.h"

#include <algorithm>
#include <array>
#include <chrono>

namespace CombatAI::Potions
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        // What a drink restores, in priority order (health first)
        enum Need : std::size_t { kHealth, kMagicka, kStamina, kNeedCount };
        constexpr std::array<RE::ActorValue, kNeedCount> kNeedAV{RE::ActorValue::kHealth, RE::ActorValue::kMagicka,
                                                                 RE::ActorValue::kStamina};
        constexpr std::array<const char *, kNeedCount> kNeedName{"health", "magicka", "stamina"};

        struct State
        {
            Clock::time_point seen{};    // last brain tick: vanilla's restore-potion pick is off while recent
            Clock::time_point drank{};   // last drink we started (cooldown)
            Clock::time_point scanned{}; // last inventory scan
            std::array<RE::AlchemyItem *, kNeedCount> potion{}; // best potion per need at that scan
            bool caster = false;         // has spells to spend magicka on (at that scan)
        };
        ThreadSafeMap<RE::FormID, State> g_state;
        bool g_otherMod = false; // NPCs Use Potions is loaded

        // How much of an actor value a potion restores: its largest Restore magnitude for it. 0 for a potion with any
        // harmful effect, and for Fortify (Peak Value Modifier) and regeneration (rate AVs), which don't restore now.
        float Restores(const RE::MagicItem *a_magic, RE::ActorValue a_av)
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
                if (base->GetArchetype() == RE::EffectSetting::Archetype::kValueModifier && base->data.primaryAV == a_av &&
                    base->data.flags.none(RE::EffectSetting::EffectSettingData::Flag::kRecover)) {
                    best = (std::max)(best, effect->GetMagnitude());
                }
            }
            return best;
        }

        bool RestoresAny(const RE::MagicItem *a_magic)
        {
            return std::ranges::any_of(kNeedAV, [a_magic](RE::ActorValue a_av) { return Restores(a_magic, a_av) > 0.0f; });
        }

        // The strongest restore potion per need it carries (shortcut: strongest, not the smallest one that's enough)
        std::array<RE::AlchemyItem *, kNeedCount> BestPotions(RE::Actor *a_actor)
        {
            std::array<RE::AlchemyItem *, kNeedCount> best{};
            std::array<float, kNeedCount> amount{};
            const auto items = a_actor->GetInventory([](RE::TESBoundObject &a_object) { return a_object.Is(RE::FormType::AlchemyItem); });
            for (const auto &[object, data] : items) {
                auto *potion = object->As<RE::AlchemyItem>();
                if (!potion || data.first <= 0 || potion->IsFood() || potion->IsPoison()) {
                    continue;
                }
                for (std::size_t n = 0; n < kNeedCount; ++n) {
                    if (const float v = Restores(potion, kNeedAV[n]); v > amount[n]) {
                        amount[n] = v;
                        best[n] = potion;
                    }
                }
            }
            return best;
        }

        bool IsCastable(const RE::TESForm *a_form)
        {
            const auto *spell = a_form ? a_form->As<RE::SpellItem>() : nullptr;
            return spell && spell->GetSpellType() == RE::MagicSystem::SpellType::kSpell &&
                   spell->GetCastingType() != RE::MagicSystem::CastingType::kConstantEffect;
        }

        // A spellcaster: a spell in hand, or a cast spell (not an ability) in its base spell list
        bool IsCaster(RE::Actor *a_actor)
        {
            if (IsCastable(a_actor->GetEquippedObject(false)) || IsCastable(a_actor->GetEquippedObject(true))) {
                return true;
            }
            auto *npc = a_actor->GetActorBase();
            const auto *list = npc ? npc->GetSpellList() : nullptr;
            if (!list || !list->spells) {
                return false;
            }
            return std::any_of(list->spells, list->spells + list->numSpells, [](const RE::SpellItem *a_spell) { return IsCastable(a_spell); });
        }

        bool Enabled()
        {
            return Config::GetInstance().GetDecisionMatrix().enableNPCPotions && !g_otherMod;
        }

        // ---- Poisons (1.3.2). Vanilla combat AI never poisons a weapon (no poison CombatInventoryItem), so NPCs
        // carrying poisons (MIFN and the like hand them out) never used them. Instant, like the player's: no animation.
        enum PoisonKind : std::size_t { kParalyze, kDamageHealth, kDamageMagicka, kDamageStamina, kOtherPoison, kPoisonKindCount };
        constexpr std::array<const char *, kPoisonKindCount> kPoisonKindName{"paralyze", "damage health", "damage magicka",
                                                                             "damage stamina", "other"};

        struct PoisonState
        {
            Clock::time_point applied{}; // last poison we put on (cooldown)
            Clock::time_point scanned{};
            std::array<RE::AlchemyItem *, kPoisonKindCount> poison{}; // strongest per kind at that scan
        };
        ThreadSafeMap<RE::FormID, PoisonState> g_poison;

        // A poison's kind (paralysis wins, else its strongest Damage Health/Magicka/Stamina) and strength in that kind
        // (paralysis: seconds; damage: magnitude x duration)
        std::pair<PoisonKind, float> Classify(const RE::AlchemyItem *a_poison)
        {
            bool paralyze = false;
            float seconds = 0.0f, damage = 0.0f, other = 0.0f;
            PoisonKind damageKind = kOtherPoison;
            for (const auto *effect : a_poison->effects) {
                const auto *base = effect ? effect->baseEffect : nullptr;
                if (!base) {
                    continue;
                }
                if (base->GetArchetype() == RE::EffectSetting::Archetype::kParalysis) {
                    paralyze = true;
                    seconds = (std::max)(seconds, static_cast<float>(effect->GetDuration()));
                    continue;
                }
                const float strength = effect->GetMagnitude() * static_cast<float>((std::max)(effect->GetDuration(), 1u));
                other = (std::max)(other, strength);
                if (!base->IsDetrimental() || base->GetArchetype() != RE::EffectSetting::Archetype::kValueModifier || strength <= damage) {
                    continue;
                }
                const auto av = base->data.primaryAV;
                const PoisonKind k = av == RE::ActorValue::kHealth    ? kDamageHealth
                                     : av == RE::ActorValue::kMagicka ? kDamageMagicka
                                     : av == RE::ActorValue::kStamina ? kDamageStamina
                                                                      : kOtherPoison;
                if (k != kOtherPoison) {
                    damageKind = k;
                    damage = strength;
                }
            }
            if (paralyze) {
                return {kParalyze, seconds};
            }
            return damageKind != kOtherPoison ? std::pair{damageKind, damage} : std::pair{kOtherPoison, other};
        }

        std::array<RE::AlchemyItem *, kPoisonKindCount> BestPoisons(RE::Actor *a_actor)
        {
            std::array<RE::AlchemyItem *, kPoisonKindCount> best{};
            std::array<float, kPoisonKindCount> strength{};
            const auto items = a_actor->GetInventory([](RE::TESBoundObject &a_object) { return a_object.Is(RE::FormType::AlchemyItem); });
            for (const auto &[object, data] : items) {
                auto *poison = object->As<RE::AlchemyItem>();
                if (!poison || data.first <= 0 || !poison->IsPoison()) {
                    continue;
                }
                if (const auto [kind, s] = Classify(poison); !best[kind] || s > strength[kind]) {
                    best[kind] = poison;
                    strength[kind] = s;
                }
            }
            return best;
        }

        // The first equipped weapon a poison can go on (right hand first): not fists, staff or bound
        RE::InventoryEntryData *PoisonableWeapon(RE::Actor *a_actor)
        {
            for (const bool left : {false, true}) {
                auto *entry = a_actor->GetEquippedEntryData(left);
                const auto *weapon = entry && entry->object ? entry->object->As<RE::TESObjectWEAP>() : nullptr;
                if (weapon && !weapon->IsHandToHandMelee() && !weapon->IsStaff() && !weapon->IsBound() && !entry->IsPoisoned()) {
                    return entry;
                }
            }
            return nullptr;
        }

        void UpdatePoison(RE::Actor *a_actor, const ActorStateData &a_state, bool a_debug)
        {
            const auto &matrix = Config::GetInstance().GetDecisionMatrix();
            if (!matrix.enableNPCPoisons || g_otherMod || !a_state.target.isValid || !a_actor->AsActorState()->IsWeaponDrawn()) {
                return;
            }
            const auto now = Clock::now();
            const RE::FormID id = a_actor->GetFormID();
            PoisonState s = g_poison.Find(id).value_or(PoisonState{});
            if (now - s.applied < std::chrono::duration<float>(matrix.poisonCooldownSeconds) || !PoisonableWeapon(a_actor)) {
                return;
            }
            auto *target = RE::TESForm::LookupByID<RE::Actor>(a_state.target.targetFormID);
            auto *targetAV = target ? ActorUtils::SafeAsActorValueOwner(target) : nullptr;
            if (!targetAV || targetAV->GetActorValue(RE::ActorValue::kPoisonResist) >= 100.0f) {
                return; // immune: keep the poison
            }
            if (now - s.scanned >= std::chrono::seconds(1)) {
                s.poison = BestPoisons(a_actor);
                s.scanned = now;
            }
            // Paralyze a strong target, drain a caster's magicka or a fighter's stamina, else damage health
            const PoisonKind want = a_state.target.isPlayer || Boss::Get(target).boss          ? kParalyze
                                    : a_state.target.isCasting || a_state.target.weaponType == WeaponType::Spell ? kDamageMagicka
                                    : a_state.target.isMelee                                                     ? kDamageStamina
                                                                                                                 : kDamageHealth;
            RE::AlchemyItem *poison = nullptr;
            PoisonKind kind = want;
            for (const PoisonKind k : {want, kDamageHealth, kParalyze, kDamageStamina, kDamageMagicka, kOtherPoison}) {
                if (s.poison[k]) {
                    poison = s.poison[k];
                    kind = k;
                    break;
                }
            }
            if (!poison) {
                g_poison.ModifyOrCreate(id, [&](PoisonState &a_entry) { a_entry = s; });
                return;
            }
            s.applied = now;
            s.scanned = {}; // rescan next time: this one may have been the last
            g_poison.ModifyOrCreate(id, [&](PoisonState &a_entry) { a_entry = s; });
            if (a_debug) {
                LOG_DEBUG("Poison 0x{:08X}: coats its weapon with {} ({}, wanted {}; target at {:.0f})", id, poison->GetName(),
                          kPoisonKindName[kind], kPoisonKindName[want], a_state.target.distance);
            }
            // shortcut: one dose; Concentrated Poison (kModPoisonDoseCount) is not applied, add it once its call is RE'd
            SKSE::GetTaskInterface()->AddTask([handle = a_actor->GetHandle(), poison]() {
                const auto actor = handle.get();
                if (!actor || actor->IsDead()) {
                    return;
                }
                const auto items = actor->GetInventory([poison](RE::TESBoundObject &a_object) { return &a_object == poison; });
                if (const auto it = items.find(poison); it == items.end() || it->second.first <= 0) {
                    return;
                }
                if (auto *entry = PoisonableWeapon(actor.get())) {
                    entry->PoisonObject(poison, 1);
                    actor->RemoveItem(poison, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                }
            });
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
                    if (s && Clock::now() - s->seen < std::chrono::seconds(2) && RestoresAny(a_item->GetMagic())) {
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
            LOG_INFO("Potions: NPCs Use Potions is loaded - it runs NPC potions and poisons, ours stay off");
        } else {
            LOG_INFO("Potions: vanilla restore-potion pick off for brain NPCs (health/magicka back off, stamina in place); poisons {}",
                     Config::GetInstance().GetDecisionMatrix().enableNPCPoisons ? "on" : "off");
        }
    }

    static void UpdateDrink(RE::Actor *a_actor, const ActorStateData &a_state, bool a_debug)
    {
        const auto now = Clock::now();
        const RE::FormID id = a_actor->GetFormID();
        State s = g_state.Find(id).value_or(State{});
        s.seen = now;
        const auto &matrix = Config::GetInstance().GetDecisionMatrix();
        const auto cooldown = std::chrono::duration<float>(matrix.potionCooldownSeconds);
        const std::array<bool, kNeedCount> low{a_state.self.healthPercent <= matrix.potionHealthThreshold,
                                               a_state.self.magickaPercent <= matrix.potionMagickaThreshold,
                                               a_state.self.isMelee && a_state.self.staminaPercent <= matrix.potionStaminaThreshold};
        const bool want = Enabled() && now - s.drank >= cooldown && std::ranges::any_of(low, std::identity{});
        if (want && now - s.scanned >= std::chrono::seconds(1)) {
            s.potion = BestPotions(a_actor);
            s.caster = IsCaster(a_actor);
            s.scanned = now;
        }
        std::size_t need = kNeedCount;
        for (std::size_t n = 0; want && n < kNeedCount && need == kNeedCount; ++n) {
            if (low[n] && s.potion[n] && (n != kMagicka || s.caster)) {
                need = n;
            }
        }
        if (need == kNeedCount) {
            Intents::EndDrinkWait(a_actor);
            g_state.ModifyOrCreate(id, [&](State &a_entry) { a_entry = s; });
            return;
        }
        auto *potion = s.potion[need];
        const float level = need == kHealth ? a_state.self.healthPercent
                          : need == kMagicka ? a_state.self.magickaPercent
                                             : a_state.self.staminaPercent;

        // Health and magicka: back off first. Stamina: drink in place (the point is the next power attack)
        const bool room = need == kStamina || !a_state.target.isValid || a_state.target.distance >= matrix.safeDrinkDistance;
        const float waited = room ? 0.0f : Intents::PostDrinkWait(a_actor);
        if (!room && waited < matrix.safeDrinkMaxWaitSeconds) {
            if (a_debug) {
                LOG_DEBUG("Potion 0x{:08X}: {} {:.0f}%, backs off to drink {} (target at {:.0f}, {:.1f} s)", id, kNeedName[need],
                          level * 100.0f, potion->GetName(), a_state.target.distance, waited);
            }
            g_state.ModifyOrCreate(id, [&](State &a_entry) { a_entry = s; });
            return;
        }

        Intents::EndDrinkWait(a_actor);
        s.drank = now;
        s.scanned = {}; // rescan next time: this one may have been the last
        g_state.ModifyOrCreate(id, [&](State &a_entry) { a_entry = s; });
        if (a_debug) {
            LOG_DEBUG("Potion 0x{:08X}: {} {:.0f}%, drinks {} ({})", id, kNeedName[need], level * 100.0f, potion->GetName(),
                      room ? std::format("target at {:.0f}", a_state.target.distance)
                           : std::format("target still at {:.0f} after {:.1f} s", a_state.target.distance, waited));
        }
        // Equip on the main thread, like NPCs Use Potions: the potion is drunk and animation mods see it
        SKSE::GetTaskInterface()->AddTask([handle = a_actor->GetHandle(), potion]() {
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

    void Update(RE::Actor *a_actor, const ActorStateData &a_state, bool a_debug)
    {
        if (!ActorUtils::SafeHasKeywordString(a_actor, "ActorTypeNPC")) {
            return; // creatures keep vanilla (no drink animations for them, no poisonable weapons)
        }
        UpdateDrink(a_actor, a_state, a_debug);
        UpdatePoison(a_actor, a_state, a_debug);
    }

    void Evict(RE::FormID a_formID)
    {
        g_state.Erase(a_formID);
        g_poison.Erase(a_formID);
    }
} // namespace CombatAI::Potions
