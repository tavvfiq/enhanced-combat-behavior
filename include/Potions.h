#pragma once

#include "pch.h"

// NPC restore potions. The brain decides when an NPC drinks: at low health (PotionHealthThreshold), or a spellcaster at
// low magicka (PotionMagickaThreshold), it backs off from its target (Intents::WantsToDrink makes EvaluateSurvival
// retreat), then drinks once SafeDrinkDistance away or after SafeDrinkMaxWaitSeconds. A melee fighter at low stamina
// (PotionStaminaThreshold) drinks where it stands. The drink is an equip (ActorEquipManager, as NPCs Use Potions does),
// so potion animation mods (Ultimate Animated Potions NG, PANG, ZUPA) animate it. Vanilla's own pick of a restore
// potion (CombatInventoryItemPotion<Restore>::CheckShouldEquip) is switched off for NPCs the brain runs, so it never
// drinks one mid-fight on its own. Off while NPCs Use Potions is loaded: that mod runs potions itself.
namespace CombatAI
{
    struct ActorStateData;
}

namespace CombatAI::Potions
{
    // Switch off vanilla's restore-potion pick for brain NPCs; call at kDataLoaded
    void Install();

    // Brain tick for an NPC (after GatherState, before the decision: it posts the drink wait the retreat reads)
    void Update(RE::Actor *a_actor, const ActorStateData &a_state, bool a_debug);

    void Evict(RE::FormID a_formID);
} // namespace CombatAI::Potions
