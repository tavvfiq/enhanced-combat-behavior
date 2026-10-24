#pragma once

#include "ActorUtils.h"
#include "pch.h"

namespace CombatAI
{
    // Weapon type classification
    enum class WeaponType : std::uint8_t
    {
        None = 0,
        Unarmed,
        OneHandedSword,
        OneHandedDagger,
        OneHandedMace,
        OneHandedAxe,
        TwoHandedSword,
        TwoHandedAxe,
        Bow,
        Crossbow,
        Staff, // Magic staff
        Spell  // Magic/spell (no weapon)
    };

    // Armor class of the body-slot item (clothing and nothing are both None)
    enum class ArmorClass : std::uint8_t
    {
        None = 0,
        Light,
        Heavy
    };

    // Self state information
    struct SelfState
    {
        float staminaPercent = 0.0f;
        float currentStamina = 0.0f; // Raw current stamina value (game units), cached once per gather
        float healthPercent = 0.0f;
        RE::ATTACK_STATE_ENUM attackState = RE::ATTACK_STATE_ENUM::kNone;
        bool isBlocking = false;
        bool isGuardCounterActive = false; // Guard counter window is active (from EldenCounter mod)
        bool isIdle = false;
        bool isSprinting = false; // Movement state: sprinting
        bool isWalking = false;   // Movement state: walking
        RE::NiPoint3 position;
        RE::NiPoint3 forwardVector;
        float boundRadius = 30.0f;                // Horizontal body radius (game units)
        WeaponType weaponType = WeaponType::None; // Weapon type equipped
        bool isOneHanded = false;                 // True if one-handed weapon
        bool isTwoHanded = false;                 // True if two-handed weapon
        bool isRanged = false;                    // True if ranged weapon (bow/crossbow)
        bool isMelee = false;                     // True if melee weapon
        ArmorClass armorClass = ArmorClass::None; // Body armor weight class
    };

    // Target state information
    struct TargetState
    {
        bool isValid = false;
        bool isPlayer = false;
        RE::FormID targetFormID = 0; // FormID of the target actor (0 if unknown)
        bool isDetected = true;
        std::int32_t detectionLevel = 0;
        bool isBlocking = false;
        bool isAttacking = false;
        bool isPowerAttacking = false;
        bool isCasting = false;
        bool isDrawingBow = false;
        bool isInAttackRecovery = false; // Target just finished attack (kFollowThrough) - recovery window
        bool isSprinting = false;        // Movement state: sprinting
        bool isWalking = false;          // Movement state: walking
        bool isFleeing = false;          // Target is fleeing/retreating from combat
        float healthPercent = 0.0f;      // Target health percentage
        float staminaPercent = 0.0f;     // Target stamina percentage
        RE::KNOCK_STATE_ENUM knockState = RE::KNOCK_STATE_ENUM::kNormal;
        float distance = 0.0f;
        float orientationDot = 0.0f; // Dot product: 1.0 = facing directly at me, -1.0 = facing away
        RE::NiPoint3 position;
        RE::NiPoint3 forwardVector;
        float boundRadius = 30.0f; // Horizontal body radius (game units)

        RE::TESForm *equippedRightHand = nullptr;
        WeaponType weaponType = WeaponType::None; // Target weapon type equipped
        bool isOneHanded = false;                 // True if target has one-handed weapon
        bool isTwoHanded = false;                 // True if target has two-handed weapon
        bool isRanged = false;                    // True if target has ranged weapon (bow/crossbow)
        bool isMelee = false;                     // True if target has melee weapon
    };

    // Threat level classification
    enum class ThreatLevel : std::uint8_t
    {
        None = 0,     // No enemies
        Low = 1,      // 1 enemy
        Moderate = 2, // 2 enemies
        High = 3,     // 3-4 enemies
        Critical = 4  // 5+ enemies
    };

    // Range classification
    enum class RangeCategory : std::uint8_t
    {
        OutOfRange = 0,   // Beyond max attack range
        MaxRange = 1,     // Within max attack range but beyond optimal
        OptimalRange = 2, // Within optimal attack range
        CloseRange = 3    // Very close (within 60% of optimal range)
    };

    // Combat context information (multiple enemies, allies, threat assessment)
    struct CombatContext
    {
        int enemyCount = 0;                // Number of enemies in combat
        int allyCount = 0;                 // Number of allies in combat (friendly actors)
        RE::Actor *closestEnemy = nullptr; // Closest enemy (might not be primary target)
        float closestEnemyDistance = 0.0f;

        // Threat level - how many enemies are targeting us
        ThreatLevel threatLevel = ThreatLevel::None;
        int enemiesTargetingUs = 0; // Number of enemies that have us as their target

        // Flanking support: track closest ally position relative to target
        RE::NiPoint3 closestAllyPosition = RE::NiPoint3(0.0f, 0.0f, 0.0f);
        float closestAllyDistance = 0.0f;
        bool hasNearbyAlly = false; // True if ally is within flanking range

        // Target facing relative to allies - for better flanking coordination
        float targetFacingAllyDot = 0.0f; // Dot product: 1.0 = target facing ally, -1.0 = target facing away from ally
        bool targetFacingAwayFromAlly =
            false; // True if target is facing away from closest ally (good flanking opportunity)
        bool targetFacingTowardAlly = false; // True if target is facing toward closest ally (we can flank from behind)
        bool allyIsPlayer = false;           // Closest ally is the player (we are a follower)

        // Weakest enemy other than the primary target (target switching finisher)
        RE::ActorHandle weakestEnemy;
        float weakestEnemyHealth = 1.0f;
        float weakestEnemyDistance = 0.0f;

        // Range granularity - precise range classification
        RangeCategory rangeCategory = RangeCategory::OutOfRange;
        bool isInAttackRange = false;  // True if within max attack range
        bool isInOptimalRange = false; // True if within optimal attack range
        bool isInCloseRange = false;   // True if very close (within 60% of optimal range)
    };

    // Temporal state information (time-based tracking for decision-making)
    struct SelfTemporalState
    {
        float timeSinceLastAttack = 999.0f;       // Time since last attack (any type)
        float timeSinceLastDodge = 999.0f;        // Time since last dodge/evasion
        float timeSinceLastEvasion = 999.0f;      // Time since last evasive dodge/jump (not forward dodge)
        float timeSinceLastAction = 999.0f;       // Time since last action (any action type)
        float blockingDuration = 0.0f;            // How long currently blocking
        float attackingDuration = 0.0f;           // How long currently attacking
        float idleDuration = 0.0f;                // How long currently idle
        float timeSinceLastPowerAttack = 999.0f;  // Time since last power attack
        float timeSinceLastSprintAttack = 999.0f; // Time since last sprint attack
        float timeSinceLastBash = 999.0f;         // Time since last bash
        float timeSinceLastFeint = 999.0f;        // Time since last feint

        // Parry feedback data
        bool lastParrySuccess = false;            // Whether last parry attempt was successful
        float lastParryEstimatedDuration = 0.0f;  // Estimated duration used for last parry attempt
        float timeSinceLastParryAttempt = 999.0f; // Time since last parry attempt
        int parrySuccessCount = 0;                // Number of successful parries (for learning)
        int parryAttemptCount = 0;                // Total number of parry attempts (for learning)

        // Timed Block feedback data
        bool lastTimedBlockSuccess = false;            // Whether last timed block attempt was successful
        float lastTimedBlockEstimatedDuration = 0.0f;  // Estimated duration used for last timed block attempt
        float timeSinceLastTimedBlockAttempt = 999.0f; // Time since last timed block attempt
        int timedBlockSuccessCount = 0;                // Number of successful timed blocks (for learning)
        int timedBlockAttemptCount = 0;                // Total number of timed block attempts (for learning)

        // Guard Counter feedback data
        bool lastGuardCounterSuccess = false;            // Whether last guard counter attempt was successful
        float timeSinceLastGuardCounterAttempt = 999.0f; // Time since last guard counter attempt
        int guardCounterSuccessCount = 0;                // Number of successful guard counters (hit)
        int guardCounterAttemptCount = 0;                // Total number of guard counter attempts
        int guardCounterFailedCount = 0;                 // Number of failed guard counters (missed/blocked)
        int guardCounterMissedOpportunityCount = 0;      // Number of times guard counter window expired without attempt
        float guardCounterSuccessRate = 0.0f;            // Success rate (0.0-1.0)

        // Attack defense feedback data (when NPC's attacks are parried/timed blocked)
        bool lastAttackParried = false;            // Whether last attack was parried
        bool lastAttackTimedBlocked = false;       // Whether last attack was timed blocked
        bool lastAttackHit = false;                // Whether last attack successfully hit the target
        bool lastAttackMissed = false;             // Whether last attack missed (no hit, not parried/timed blocked)
        float timeSinceLastParriedAttack = 999.0f; // Time since last parried attack
        float timeSinceLastTimedBlockedAttack = 999.0f; // Time since last timed blocked attack
        float timeSinceLastHitAttack = 999.0f;          // Time since last successful hit
        float timeSinceLastMissedAttack = 999.0f;       // Time since last missed attack
        int parriedAttackCount = 0;                     // Number of attacks that were parried
        int timedBlockedAttackCount = 0;                // Number of attacks that were timed blocked
        int hitAttackCount = 0;                         // Number of attacks that successfully hit
        int missedAttackCount = 0;                      // Number of attacks that missed
        int totalAttackCount = 0;                       // Total number of attacks attempted
        float parryRate = 0.0f;                         // Percentage of attacks parried (0.0-1.0)
        float timedBlockRate = 0.0f;                    // Percentage of attacks timed blocked (0.0-1.0)
        float hitRate = 0.0f;                           // Percentage of attacks that hit (0.0-1.0)
        float missRate = 0.0f;                          // Percentage of attacks that missed (0.0-1.0)
        float totalDefenseRate = 0.0f;                  // Combined parry + timed block rate

        // Damage momentum: smoothed fraction of target max health removed per hit, and
        // whether enough data exists to trust it. Drives aggression scaling.
        float recentDamageFraction = 0.0f;
        bool hasMomentumData = false;

        // Damage race: average hit (fraction of the victim's max health) it landed on its target, and the
        // target on it, over the whole fight (0 = not measured yet; needs two hits)
        float ourAverageHit = 0.0f;
        float theirAverageHit = 0.0f;
    };

    struct TargetTemporalState
    {
        float timeSinceLastAttack = 999.0f;      // Time since target last attacked
        float blockingDuration = 0.0f;           // How long target has been blocking
        float recentBlockCommitments = 0.0f;     // Recent block starts, decayed over time
        float castingDuration = 0.0f;            // How long target has been casting
        float drawingDuration = 0.0f;            // How long target has been drawing bow
        float attackingDuration = 0.0f;          // How long target has been attacking
        float idleDuration = 0.0f;               // How long target has been idle
        float timeSinceLastPowerAttack = 999.0f; // Time since target's last power attack

        // Parry timing data
        float attackStartTime = -1.0f;        // When current attack started (relative time, -1 if not attacking)
        float estimatedAttackDuration = 0.0f; // Estimated total duration of current attack
        float timeUntilAttackHits = 999.0f;   // Estimated time until attack hits (999 if not attacking or already hit)
        std::uint32_t swingCount = 0;         // Counts the target's swings: the key for once-per-swing rolls
    };

    struct TemporalState
    {
        SelfTemporalState self;
        TargetTemporalState target;
    };

    // A projectile from the engine's threat list on course to hit this actor
    struct IncomingProjectile
    {
        bool valid = false;
        float timeToImpact = 0.0f; // seconds to closest approach
        float speed = 0.0f;
        RE::NiPoint3 flightDir;    // horizontal unit vector
        RE::NiPoint3 missOffset;   // from the flight line to the actor at closest approach
        RE::FormID threatID = 0;
        RE::FormID shooterID = 0;
    };

    // Environmental sensing (raycast-derived spatial awareness). Defaults are
    // optimistic so an actor behaves normally when sensing is disabled or hasn't run.
    struct EnvState
    {
        bool losChecked = false;  // whether line-of-sight sensing ran this gather
        bool losToTarget = true;  // clear line of sight to the primary target (no wall between)
    };

    // Combined state data for decision making
    struct ActorStateData
    {
        SelfState self;
        TargetState target;
        CombatContext combatContext; // Combat context (multiple enemies, etc.)
        TemporalState temporal;      // Temporal state (time-based tracking)
        EnvState env;                // Environmental sensing (line-of-sight, etc.)
        IncomingProjectile incomingProjectile; // From the engine's threat list
        float deltaTime = 0.0f;
        float weaponReach = 150.0f; // Weapon reach (from Precision or fallback)
        bool weaponReachFromPrecision = false;
    };

    // Helper functions
    namespace StateHelpers
    {
        // Calculate distance between two positions
        inline float CalculateDistance(const RE::NiPoint3 &a, const RE::NiPoint3 &b) { return a.GetDistance(b); }

        // Effective melee reach in game units. target.distance is center-to-center,
        // so pad the (multiplier-scaled) weapon reach with the target's physical body
        // radius. Single source of truth shared by the observer's range categories and
        // the decision matrix's attack checks so they never disagree.
        inline float EffectiveAttackRange(float a_weaponReach, float a_targetBoundRadius, float a_multiplier,
                                          float a_safetyMargin = 0.0f)
        {
            const float effectiveRange = a_weaponReach * a_multiplier + a_targetBoundRadius - a_safetyMargin;
            return effectiveRange > 0.0f ? effectiveRange : 0.0f;
        }

        // Calculate dot product for orientation check
        // Returns 1.0 if target is directly facing self, -1.0 if facing away
        inline float CalculateOrientationDot(const RE::NiPoint3 &selfPos, const RE::NiPoint3 &targetPos,
                                             const RE::NiPoint3 &targetForward)
        {
            RE::NiPoint3 toSelf = (selfPos - targetPos);
            toSelf.Unitize();
            return toSelf.Dot(targetForward);
        }

        // Get forward vector from actor (approximation using heading) - uses safe wrapper
        inline RE::NiPoint3 GetActorForwardVector(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return RE::NiPoint3(0.0f, 1.0f, 0.0f);
            }

            // Get rotation (heading) and convert to forward vector - use safe wrapper
            auto headingOpt = ActorUtils::SafeGetHeading(a_actor, false);
            if (headingOpt.has_value()) {
                float heading = headingOpt.value();
                float cosH = std::cos(heading);
                float sinH = std::sin(heading);
                return RE::NiPoint3(sinH, cosH, 0.0f);
            }

            // Fallback if heading access failed
            return RE::NiPoint3(0.0f, 1.0f, 0.0f);
        }

        // Identify weapon type from TESObjectWEAP
        inline WeaponType IdentifyWeaponType(RE::TESObjectWEAP *a_weapon)
        {
            if (!a_weapon) {
                return WeaponType::None;
            }

            // Check for ranged weapons first
            if (a_weapon->IsBow()) {
                return WeaponType::Bow;
            }
            if (a_weapon->IsCrossbow()) {
                return WeaponType::Crossbow;
            }

            // Check weapon type using weaponData.animationType
            // RE::WEAPON_TYPE is an enumeration wrapper, need to get underlying value
            auto weaponTypeValue = a_weapon->weaponData.animationType.underlying();

            // RE::WEAPON_TYPE enum values:
            // kHandToHand = 0, kOneHandSword = 1, kOneHandDagger = 2, kOneHandAxe = 3,
            // kOneHandMace = 4, kTwoHandSword = 5, kTwoHandAxe = 6, kBow = 7, kCrossbow = 8, kStaff = 9
            switch (weaponTypeValue) {
            case 0: // kHandToHand
                return WeaponType::Unarmed;
            case 1: // kOneHandSword
                return WeaponType::OneHandedSword;
            case 2: // kOneHandDagger
                return WeaponType::OneHandedDagger;
            case 3: // kOneHandAxe
                return WeaponType::OneHandedAxe;
            case 4: // kOneHandMace
                return WeaponType::OneHandedMace;
            case 5: // kTwoHandSword
                return WeaponType::TwoHandedSword;
            case 6: // kTwoHandAxe
                return WeaponType::TwoHandedAxe;
            case 7: // kBow
                return WeaponType::Bow;
            case 8: // kCrossbow
                return WeaponType::Crossbow;
            case 9: // kStaff
                return WeaponType::Staff;
            default:
                return WeaponType::None;
            }
        }

        // Get weapon type from actor (checks equipped weapon)
        inline WeaponType GetActorWeaponType(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return WeaponType::None;
            }

            // Check right hand first
            auto weapon = ActorUtils::SafeGetEquippedObject(a_actor, false);
            if (weapon && weapon->IsWeapon()) {
                auto weap = weapon->As<RE::TESObjectWEAP>();
                if (weap) {
                    return IdentifyWeaponType(weap);
                }
            }

            // Check left hand (for dual wielding or spell)
            weapon = ActorUtils::SafeGetEquippedObject(a_actor, true);
            if (weapon) {
                if (weapon->IsWeapon()) {
                    auto weap = weapon->As<RE::TESObjectWEAP>();
                    if (weap) {
                        return IdentifyWeaponType(weap);
                    }
                } else {
                    // Left hand has spell/magic
                    return WeaponType::Spell;
                }
            }

            // Check if casting magic
            if (ActorUtils::SafeWhoIsCasting(a_actor) != 0) {
                return WeaponType::Spell;
            }

            // No weapon equipped - unarmed
            return WeaponType::Unarmed;
        }
    } // namespace StateHelpers
} // namespace CombatAI
