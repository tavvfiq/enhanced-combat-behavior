#pragma once

#include "pch.h"

// Limb reach (1.22.0): how far each of a beast's attacks reaches, read from its own skeleton. At the first blow of
// a swing (its clip's HitFrame annotation) the farther hand's distance from the beast's center is that attack's
// reach. Kept per race and attack event (median of the last 5 swings, per unit of actor scale), so a race's attacks
// are known after each is played once, wherever the beast was allowed to swing from (the contact-learned reach can
// only learn distances it already attacks from).
//
// Used with Precision only (its hand capsules make the hand the hit point; vanilla's own hit check uses the race's
// unarmed reach instead): the brain closes to the longest known attack's reach, and VanillaAttackShaper lets an
// attack start only when it reaches, and a short one only when no longer one of its kind reaches (more margin).
namespace CombatAI::LimbReach
{
    // Hooks the Character anim event sink (VTABLE_Character[2] slot 1) for HitFrame
    void Install();

    // EngineProbes, on an accepted attack start event: the swing whose first blow will be measured
    void NoteAttackStart(RE::Actor *a_actor, std::string_view a_event);

    // Hand reach of a_actor's attack a_event (center to hand, game units at its scale); 0 until measured
    float Reach(RE::Actor *a_actor, std::string_view a_event);

    // Seconds from a_actor's attack a_event starting to its first blow (median); 0 until measured
    float Startup(RE::Actor *a_actor, std::string_view a_event);

    // Center-to-center distance to a_target when a blow a_startup seconds away lands: now, less the target's speed
    // toward a_actor over that time (a target backing off is farther by then)
    float DistanceAtBlow(RE::Actor *a_actor, RE::Actor *a_target, float a_startup);

    // Longest measured reach over a_actor's race attacks (bashes excluded); 0 when none measured
    float LongestReach(RE::Actor *a_actor);

    // Center-to-center distance up to which an attack of hand reach a_reach touches a target of body radius
    // a_targetRadius
    float ContactDistance(float a_reach, float a_targetRadius);

    // Applies: a beast creature, Precision loaded, LearnUnarmedReach on
    bool Active(RE::Actor *a_actor);

    // A brain creature mid-swing, so no new attack: a beast with limb reach until its swing's first blow (then a combo
    // may follow), other brain creatures (draugr) for CreatureSwingSeconds after the start (their graph refuses sooner)
    bool CreatureStillSwinging(RE::Actor *a_actor);

    // Some measured attack of a_actor's race (of the power kind when a_power, else light; bashes excluded, chance > 0)
    // reaches a_target at its blow (DistanceAtBlow), with a reach over a_longerThan when that is > 0
    bool AnyReaches(RE::Actor *a_actor, RE::Actor *a_target, bool a_power, float a_longerThan = 0.0f);
} // namespace CombatAI::LimbReach
