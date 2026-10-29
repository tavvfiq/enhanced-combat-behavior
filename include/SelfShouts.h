#pragma once

#include "pch.h"

// Self-buff shouts. Vanilla's combat AI only uses a shout whose effect has a row in its archetype -> caster table
// (Stagger, Disarm, Paralysis, damage, summons...); Become Ethereal, Elemental Fury / Battle Fury and Slow Time have
// none, so NPCs that know them never shout them. The brain starts them through the same call vanilla's CastShout
// leaf uses (SE 37850 / AE 38804: claims the process shout slot, then the voice caster's RequestCast, so the shout
// animation, voice recovery and sound are vanilla's). Become Ethereal at low health (it pairs with backing off to
// drink), weapon-speed shouts on engage in melee, Slow Time (off by default) at low health or outnumbered. Never while
// the voice recovers or the effect is already on.
namespace CombatAI
{
    struct ActorStateData;
}

namespace CombatAI::SelfShouts
{
    // Brain tick for an NPC (after GatherState)
    void Update(RE::Actor *a_actor, const ActorStateData &a_state, bool a_debug);

    void Evict(RE::FormID a_formID);
} // namespace CombatAI::SelfShouts
