#include "pch.h"

// Undefine Windows macros that conflict with std functions
// Must be done after pch.h (which includes RE/Skyrim.h that may include Windows
// headers)
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifdef clamp
#undef clamp
#endif

#include "ActorStateObserver.h"
#include "ProjectileAwareness.h"
#include "ActorUtils.h"
#include "AttackDefenseFeedbackTracker.h"
#include "Config.h"
#include "EnvSensor.h"
#include "GuardCounterFeedbackTracker.h"
#include "ParryFeedbackTracker.h"
#include "PrecisionIntegration.h"
#include "TimedBlockFeedbackTracker.h"

namespace CombatAI
{
    // Helper function to clamp values (avoids Windows macro issues)
    template <typename T> constexpr T ClampValue(const T &value, const T &minVal, const T &maxVal)
    {
        return (value < minVal) ? minVal : ((value > maxVal) ? maxVal : value);
    }
    ActorStateData ActorStateObserver::GatherState(RE::Actor *a_actor, float a_deltaTime)
    {
        ActorStateData data;
        data.deltaTime = a_deltaTime;

        if (!a_actor) {
            return data;
        }

        // Gather self state
        data.self = GatherSelfState(a_actor);

        // Get weapon reach
        data.weaponReach = GetWeaponReach(a_actor, &data.weaponReachFromPrecision);

        // Gather target state. Hold the NiPointer for the whole gather so the target
        // can't be released while we still use it below.
        RE::NiPointer<RE::Actor> targetPtr;
        if (RE::CombatController *combatController = ActorUtils::SafeGetCombatController(a_actor)) {
            targetPtr = combatController->targetHandle.get();
        }
        RE::Actor *target = targetPtr.get();
        if (target) {
            data.target = GatherTargetState(a_actor, target);
        }

        // Gather combat context (multiple enemies, etc.) - cached for performance
        data.combatContext = GatherCombatContext(a_actor, m_currentTime, data.weaponReach);

        // Gather temporal state (time-based tracking)
        data.temporal = GatherTemporalState(a_actor, target, a_deltaTime, data);

        if (Config::GetInstance().GetProjectileAwareness().enableProjectileDodge) {
            data.incomingProjectile = ProjectileAwareness::GetInstance().Assess(
                a_actor->GetFormID(), data.self.position, data.self.boundRadius);
        }

        // Environmental sensing: line of sight to the primary target. A single Havok
        // LOS raycast, run only when we have a target and the gate is enabled. Gathered
        // here (on the game thread, already throttled by the process/reaction cadence)
        // so the decision matrix can veto swinging at a target behind cover.
        const auto &envCfg = Config::GetInstance().GetEnvSensor();
        if (envCfg.enableLosGate && target && data.target.isValid) {
            data.env.losToTarget = EnvSensor::HasLineOfSight(a_actor, target, envCfg.losEyeHeight);
            data.env.losChecked = true;
        }

        return data;
    }

    void ActorStateObserver::Update(float a_deltaTime)
    {
        m_currentTime += a_deltaTime;

        m_cacheSweepTimer += a_deltaTime;
        if (m_cacheSweepTimer >= CACHE_SWEEP_INTERVAL) {
            m_cacheSweepTimer = 0.0f;
            PruneStaleCaches();
        }
    }

    void ActorStateObserver::PruneStaleCaches()
    {
        const float now = m_currentTime;
        m_detectionCache.WithWriteLock([&](auto &map) {
            for (auto it = map.begin(); it != map.end();) {
                if (now - it->second.lastUpdateTime > CACHE_STALE_THRESHOLD) {
                    it = map.erase(it);
                } else {
                    ++it;
                }
            }
        });
        m_combatContextCache.WithWriteLock([&](auto &map) {
            for (auto it = map.begin(); it != map.end();) {
                if (now - it->second.lastUpdateTime > CACHE_STALE_THRESHOLD) {
                    it = map.erase(it);
                } else {
                    ++it;
                }
            }
        });
    }

    SelfState ActorStateObserver::GatherSelfState(RE::Actor *a_actor)
    {
        SelfState state;

        if (!a_actor) {
            return state;
        }

        // Stamina percentage
        state.staminaPercent = GetActorValuePercent(a_actor, RE::ActorValue::kStamina);

        // Raw current stamina (cached once so the decision matrix doesn't re-fetch it repeatedly)
        {
            auto avOwner = ActorUtils::SafeAsActorValueOwner(a_actor);
            state.currentStamina = avOwner ? avOwner->GetActorValue(RE::ActorValue::kStamina) : 0.0f;
        }

        // Health percentage
        state.healthPercent = GetActorValuePercent(a_actor, RE::ActorValue::kHealth);

        // Attack state - use safe wrapper
        state.attackState = ActorUtils::SafeGetAttackState(a_actor);
        state.isIdle = (state.attackState == RE::ATTACK_STATE_ENUM::kNone);

        // Blocking state - use safe wrapper
        state.isBlocking = ActorUtils::SafeIsBlocking(a_actor);

        // Guard counter state (from EldenCounter mod)
        bool guardCounterActive = false;
        ActorUtils::SafeGetGraphVariableBool(a_actor, "Val_GuardCounter", guardCounterActive);
        state.isGuardCounterActive = guardCounterActive;

        // Movement state - sprinting/walking - use safe wrappers
        state.isSprinting = ActorUtils::SafeIsSprinting(a_actor);
        state.isWalking = ActorUtils::SafeIsWalking(a_actor);

        // Position - use safe wrapper
        auto posOpt = ActorUtils::SafeGetPosition(a_actor);
        if (posOpt.has_value()) {
            state.position = posOpt.value();
        } else {
            state.position = RE::NiPoint3(0.0f, 0.0f, 0.0f);
        }

        // Forward vector - GetActorForwardVector now uses safe wrapper internally
        state.forwardVector = StateHelpers::GetActorForwardVector(a_actor);

        // Physical body radius (used to pad melee reach)
        state.boundRadius = ActorUtils::GetBodyRadius(a_actor);

        // Weapon type information
        state.weaponType = StateHelpers::GetActorWeaponType(a_actor);

        // Set weapon category flags
        state.isOneHanded =
            (state.weaponType == WeaponType::OneHandedSword || state.weaponType == WeaponType::OneHandedDagger ||
             state.weaponType == WeaponType::OneHandedMace || state.weaponType == WeaponType::OneHandedAxe);
        state.isTwoHanded =
            (state.weaponType == WeaponType::TwoHandedSword || state.weaponType == WeaponType::TwoHandedAxe);
        state.isRanged = (state.weaponType == WeaponType::Bow || state.weaponType == WeaponType::Crossbow);
        state.isMelee = (state.isOneHanded || state.isTwoHanded || state.weaponType == WeaponType::Unarmed ||
                         state.weaponType == WeaponType::Staff);

        // Body armor class from the worn biped (cheap; GetWornArmor walks the whole inventory)
        if (const auto &biped = a_actor->GetCurrentBiped()) {
            const auto *item = biped->objects[RE::BIPED_OBJECTS::kBody].item;
            if (const auto *armor = item ? item->As<RE::TESObjectARMO>() : nullptr) {
                state.armorClass = armor->IsHeavyArmor()   ? ArmorClass::Heavy
                                   : armor->IsLightArmor() ? ArmorClass::Light
                                                           : ArmorClass::None;
            }
        }

        return state;
    }

    TargetState ActorStateObserver::GatherTargetState(RE::Actor *a_self, RE::Actor *a_target)
    {
        TargetState state;

        if (!a_self || !a_target) {
            return state;
        }

        state.isValid = true;
        state.isPlayer = ActorUtils::SafeIsPlayerRef(a_target);
        {
            auto tgtFormIDOpt = ActorUtils::SafeGetFormID(a_target);
            state.targetFormID = tgtFormIDOpt.has_value() ? tgtFormIDOpt.value() : 0;
        }
        if (state.isPlayer) {
            // Detection is throttled per observer: RequestDetectionLevel is expensive and
            // crash-prone, and stealth state doesn't change meaningfully within 0.5s.
            auto selfIDOpt = ActorUtils::SafeGetFormID(a_self);
            RE::FormID selfID = selfIDOpt.has_value() ? selfIDOpt.value() : 0;

            std::int32_t detection = 0;
            bool haveFresh = false;
            if (selfID != 0) {
                auto cached = m_detectionCache.Find(selfID);
                if (cached.has_value() && (m_currentTime - cached->lastUpdateTime) < DETECTION_UPDATE_INTERVAL) {
                    detection = cached->level;
                    haveFresh = true;
                }
            }
            if (!haveFresh) {
                detection = ActorUtils::SafeRequestDetectionLevel(a_self, a_target);
                if (selfID != 0) {
                    m_detectionCache.ModifyOrCreate(selfID, [&](CachedDetection &c) {
                        c.level = detection;
                        c.lastUpdateTime = m_currentTime;
                    });
                }
            }
            state.detectionLevel = detection;
            state.isDetected = detection > 0;
        }

        // Use safe wrappers for all target property access - target can be knocked
        // down/deleted Blocking state - use safe wrapper
        state.isBlocking = ActorUtils::SafeIsBlocking(a_target);

        // Attacking state - use safe wrapper
        state.isAttacking = ActorUtils::SafeIsAttacking(a_target);

        // Power attacking - IsPowerAttacking already has internal error handling
        state.isPowerAttacking = IsPowerAttacking(a_target);

        // Casting - use safe wrapper
        state.isCasting = (ActorUtils::SafeWhoIsCasting(a_target) != 0);

        // Drawing bow - check if target has bow/crossbow and is in draw state - use
        // safe wrappers
        state.isDrawingBow = false;
        auto equippedWeapon = ActorUtils::SafeGetEquippedObject(a_target, false);
        if (equippedWeapon && equippedWeapon->IsWeapon()) {
            auto weapon = equippedWeapon->As<RE::TESObjectWEAP>();
            if (weapon && (weapon->IsBow() || weapon->IsCrossbow())) {
                RE::ATTACK_STATE_ENUM attackState = ActorUtils::SafeGetAttackState(a_target);
                // kDraw means they're drawing the bow
                state.isDrawingBow = (attackState == RE::ATTACK_STATE_ENUM::kDraw);
            }
        }

        // Attack recovery state - check if target just finished attack
        // (kFollowThrough) - use safe wrapper
        RE::ATTACK_STATE_ENUM targetAttackState = ActorUtils::SafeGetAttackState(a_target);
        state.isInAttackRecovery = (targetAttackState == RE::ATTACK_STATE_ENUM::kFollowThrough);

        // Target health and stamina percentages
        state.healthPercent = GetActorValuePercent(a_target, RE::ActorValue::kHealth);
        state.staminaPercent = GetActorValuePercent(a_target, RE::ActorValue::kStamina);

        // Target movement state - sprinting/walking - use safe wrappers
        state.isSprinting = ActorUtils::SafeIsSprinting(a_target);
        state.isWalking = ActorUtils::SafeIsWalking(a_target);

        // Target fleeing state - check if target is fleeing from combat - use safe
        // wrapper
        state.isFleeing = ActorUtils::SafeIsFleeing(a_target);

        // Knock state (stagger/recoil) - use safe wrapper (can crash when actor is
        // knocked down)
        state.knockState = ActorUtils::SafeGetKnockState(a_target);

        // Position - use safe wrapper
        auto posOpt = ActorUtils::SafeGetPosition(a_target);
        if (posOpt.has_value()) {
            state.position = posOpt.value();
        } else {
            return state; // Can't get position, return incomplete state
        }

        // Forward vector - GetActorForwardVector now uses safe wrapper internally
        state.forwardVector = StateHelpers::GetActorForwardVector(a_target);

        // Physical body radius (used to pad melee reach)
        state.boundRadius = ActorUtils::GetBodyRadius(a_target);

        // Distance - use safe wrapper for self position
        auto selfPosOpt = ActorUtils::SafeGetPosition(a_self);
        if (selfPosOpt.has_value()) {
            state.distance = StateHelpers::CalculateDistance(selfPosOpt.value(), state.position);
        } else {
            state.distance = 0.0f;
        }

        // Orientation dot product (is target looking at me?) - use safe wrapper for
        // self position
        auto selfPosOpt2 = ActorUtils::SafeGetPosition(a_self);
        if (selfPosOpt2.has_value()) {
            state.orientationDot =
                StateHelpers::CalculateOrientationDot(selfPosOpt2.value(), state.position, state.forwardVector);
        } else {
            state.orientationDot = 0.0f;
        }

        // equipped weapons - use safe wrapper
        state.equippedRightHand = ActorUtils::SafeGetEquippedObject(a_target, false);

        // Weapon type information
        state.weaponType = StateHelpers::GetActorWeaponType(a_target);

        // Set weapon category flags
        state.isOneHanded =
            (state.weaponType == WeaponType::OneHandedSword || state.weaponType == WeaponType::OneHandedDagger ||
             state.weaponType == WeaponType::OneHandedMace || state.weaponType == WeaponType::OneHandedAxe);
        state.isTwoHanded =
            (state.weaponType == WeaponType::TwoHandedSword || state.weaponType == WeaponType::TwoHandedAxe);
        state.isRanged = (state.weaponType == WeaponType::Bow || state.weaponType == WeaponType::Crossbow);
        state.isMelee = (state.isOneHanded || state.isTwoHanded || state.weaponType == WeaponType::Unarmed ||
                         state.weaponType == WeaponType::Staff);

        return state;
    }

    float ActorStateObserver::GetActorValuePercent(RE::Actor *a_actor, RE::ActorValue a_value)
    {
        if (!a_actor) {
            return 0.0f;
        }

        // Use safe wrapper to access ActorValueOwner
        // In CommonLibSSE, Actor inherits from ActorValueOwner, so we cast directly
        auto actorValueOwner = ActorUtils::SafeAsActorValueOwner(a_actor);
        if (!actorValueOwner) {
            return 0.0f;
        }

        float current = actorValueOwner->GetActorValue(a_value);

        // Calculate maximum value - use GetActorValueMax which includes permanent +
        // temporary modifiers
        float max = 0.0f;
        // Try GetActorValueMax first (includes permanent + temporary modifiers)
        max = a_actor->GetActorValueMax(a_value);

        // If GetActorValueMax returned 0 or failed, try manual calculation
        // GetPermanentActorValue + GetActorValueModifier(kTemporary)
        if (max <= 0.0f) {
            float permanent = actorValueOwner->GetPermanentActorValue(a_value);
            float temporary = a_actor->GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary, a_value);
            max = permanent + temporary;
        }

        // Final fallback: Just use GetPermanentActorValue
        if (max <= 0.0f) {
            max = actorValueOwner->GetPermanentActorValue(a_value);
        }

        // Last resort: GetBaseActorValue
        if (max <= 0.0f) {
            max = actorValueOwner->GetBaseActorValue(a_value);
        }

        if (max <= 0.0f) {
            return 0.0f;
        }

        return ClampValue(current / max, 0.0f, 1.0f);
    }

    bool ActorStateObserver::IsPowerAttacking(RE::Actor *a_target)
    {
        if (!a_target) {
            return false;
        }

        // Proper power attack detection using AttackData from HighProcessData
        // Based on ProjectGapClose implementation
        auto currentProcess = a_target->GetActorRuntimeData().currentProcess;
            if (currentProcess) {
            auto highProcess = currentProcess->high;
            if (highProcess) {
                auto attackData = highProcess->attackData;
                if (attackData) {
                    auto flags = attackData->data.flags;
                    return flags.any(RE::AttackData::AttackFlag::kPowerAttack);
                }
            }
        }

        return false;
    }

    float ActorStateObserver::EstimateAttackDuration(RE::Actor *a_target, bool a_isPowerAttack)
    {
        if (!a_target) {
            return 0.5f; // Default attack duration
        }

        float baseDuration = 0.5f; // Base attack duration in seconds

        // Get weapon speed if available
        auto equippedWeapon = ActorUtils::SafeGetEquippedObject(a_target, false);
        if (equippedWeapon && equippedWeapon->IsWeapon()) {
            auto weapon = equippedWeapon->As<RE::TESObjectWEAP>();
            if (weapon) {
                float weaponSpeed = weapon->GetSpeed();
                // Weapon speed is typically 0.5-2.0, normalize to duration
                // Faster weapons = shorter duration
                baseDuration = 0.6f / (std::max)(weaponSpeed, 0.1f);
            }
        }

        // Apply power attack multiplier (power attacks are slower)
        if (a_isPowerAttack) {
            baseDuration *= 1.5f;
        }

        // Apply weapon type multipliers
        WeaponType weaponType = StateHelpers::GetActorWeaponType(a_target);
        switch (weaponType) {
        case WeaponType::TwoHandedSword:
        case WeaponType::TwoHandedAxe:
            baseDuration *= 1.2f; // Two-handed weapons are slower
            break;
        case WeaponType::OneHandedDagger:
            baseDuration *= 0.8f; // Daggers are faster
            break;
        case WeaponType::Unarmed:
            baseDuration *= 0.9f; // Unarmed is fast
            break;
        default:
            break;
        }

        // Apply actor speed multiplier
        // Use safe wrapper to access ActorValueOwner
        auto actorValueOwner = ActorUtils::SafeAsActorValueOwner(a_target);
        if (actorValueOwner) {
            float speedMult = actorValueOwner->GetActorValue(RE::ActorValue::kSpeedMult);
            if (speedMult > 0.0f) {
                baseDuration /= (speedMult / 100.0f);
            }
        }

        // Use feedback from previous parry attempts to improve estimation
        // Get feedback for this target (they were parried by someone)
        // We'll use feedback from any actor who parried this target
        // For now, we'll use a simple approach: if we have recent feedback, adjust
        // slightly
        auto parryFeedback = ParryFeedbackTracker::GetInstance().GetFeedback(a_target);
        if (parryFeedback.parryAttemptCount > 0 && parryFeedback.lastParryEstimatedDuration > 0.0f) {
            // We have feedback - use it to adjust estimation
            // If last parry was successful, our estimation was good (or close)
            // If last parry failed, our estimation might be off
            float feedbackAdjustment = 1.0f;

            if (parryFeedback.lastParrySuccess) {
                // Successful parry - our estimation was good, but we can still refine
                // Use weighted average: 70% current estimate, 30% successful estimate
                float successRate =
                    parryFeedback.parrySuccessCount / static_cast<float>(parryFeedback.parryAttemptCount);
                if (successRate > 0.5f) {
                    // High success rate - trust the feedback more
                    baseDuration = baseDuration * 0.7f + parryFeedback.lastParryEstimatedDuration * 0.3f;
                }
            } else {
                // Failed parry - our estimation might be off
                // Adjust based on how far off we were (if we have that data)
                // For now, just use a small adjustment
                feedbackAdjustment = 1.05f; // Slightly increase duration estimate
            }

            baseDuration *= feedbackAdjustment;
        }

        // Use feedback from previous timed block attempts to improve estimation
        // Similar to parry feedback, but for timed blocks
        auto timedBlockFeedback = TimedBlockFeedbackTracker::GetInstance().GetFeedback(a_target);
        if (timedBlockFeedback.timedBlockAttemptCount > 0 &&
            timedBlockFeedback.lastTimedBlockEstimatedDuration > 0.0f) {
            // We have feedback - use it to adjust estimation
            float feedbackAdjustment = 1.0f;

            if (timedBlockFeedback.lastTimedBlockSuccess) {
                // Successful timed block - our estimation was good
                float successRate = timedBlockFeedback.timedBlockSuccessCount /
                                    static_cast<float>(timedBlockFeedback.timedBlockAttemptCount);
                if (successRate > 0.5f) {
                    // High success rate - trust the feedback more
                    baseDuration = baseDuration * 0.7f + timedBlockFeedback.lastTimedBlockEstimatedDuration * 0.3f;
                }
            } else {
                // Failed timed block - our estimation might be off
                feedbackAdjustment = 1.05f; // Slightly increase duration estimate
            }

            baseDuration *= feedbackAdjustment;
        }

        // Clamp to reasonable range (0.2s to 2.0s)
        return ClampValue(baseDuration, 0.2f, 2.0f);
    }

    float ActorStateObserver::GetWeaponReach(RE::Actor *a_actor, bool *a_fromPrecision)
    {
        if (!a_actor) {
            if (a_fromPrecision) {
                *a_fromPrecision = false;
            }
            return 150.0f; // Default reach in game units
        }

        // Single source of truth: PrecisionIntegration returns the Precision capsule
        // length when the mod is available, otherwise its vanilla weapon-reach
        // fallback (m_precisionAPI is only set when Precision integration is enabled).
        return PrecisionIntegration::GetInstance().GetWeaponReach(a_actor, a_fromPrecision);
    }

    void ActorStateObserver::UpdateRangeCategory(RE::Actor *a_actor, CombatContext &a_context, float a_weaponReach)
    {
        if (!a_actor) {
            return;
        }

        // Resolve the live primary combat target
        RE::Actor *target = nullptr;
        RE::NiPointer<RE::Actor> targetPtr;
        auto *combatController = ActorUtils::SafeGetCombatController(a_actor);
            if (combatController) {
            targetPtr = combatController->targetHandle.get();
            target = targetPtr.get();
        }
        if (!target) {
            return;
        }

        auto selfPosOpt = ActorUtils::SafeGetPosition(a_actor);
        auto targetPosOpt = ActorUtils::SafeGetPosition(target);
        if (!selfPosOpt.has_value() || !targetPosOpt.has_value()) {
            return;
        }

        float targetDistance = StateHelpers::CalculateDistance(selfPosOpt.value(), targetPosOpt.value());
        a_context.closestEnemyDistance = targetDistance;

        float weaponReach = a_weaponReach;
        if (weaponReach <= 0.0f) {
            weaponReach = 150.0f; // Fallback
        }

        // "In attack range" uses the same formula as the decision matrix's offense
        // check (weapon reach * offense multiplier + target body radius) so
        // isInAttackRange never disagrees with whether an attack will be attempted.
        // Optimal/close are tiers of that range.
        float targetRadius = ActorUtils::GetBodyRadius(target);
        float offenseMult = Config::GetInstance().GetDecisionMatrix().offenseReachMultiplier;
        float safetyMargin = Config::GetInstance().GetDecisionMatrix().offenseReachSafetyMargin;
        float maxAttackRange =
            StateHelpers::EffectiveAttackRange(weaponReach, targetRadius, offenseMult, safetyMargin);
        float optimalAttackRange = maxAttackRange * 0.9f; // Optimal attack range (90% of max)
        float closeRange = maxAttackRange * 0.54f;        // Close range threshold (~54% of max)

        a_context.isInCloseRange = false;
        a_context.isInOptimalRange = false;
        a_context.isInAttackRange = false;
        if (targetDistance <= closeRange) {
            a_context.rangeCategory = RangeCategory::CloseRange;
            a_context.isInCloseRange = true;
            a_context.isInOptimalRange = true;
            a_context.isInAttackRange = true;
        } else if (targetDistance <= optimalAttackRange) {
            a_context.rangeCategory = RangeCategory::OptimalRange;
            a_context.isInOptimalRange = true;
            a_context.isInAttackRange = true;
        } else if (targetDistance <= maxAttackRange) {
            a_context.rangeCategory = RangeCategory::MaxRange;
            a_context.isInAttackRange = true;
        } else {
            a_context.rangeCategory = RangeCategory::OutOfRange;
        }
    }

    CombatContext ActorStateObserver::GatherCombatContext(RE::Actor *a_actor, float a_currentTime, float a_weaponReach)
    {
        CombatContext context;

        if (!a_actor) {
            return context;
        }

        // Get FormID safely - use FormID as key instead of raw pointer
        auto formIDOpt = ActorUtils::SafeGetFormID(a_actor);
        if (!formIDOpt.has_value()) {
            return context; // Can't get FormID, actor is invalid
        }
        RE::FormID formID = formIDOpt.value();

        // Validate actor before accessing cache
        if (ActorUtils::SafeIsDead(a_actor) || !ActorUtils::SafeIsInCombat(a_actor)) {
            // Actor is invalid, clean up cache and return empty context
            m_combatContextCache.Erase(formID);
            return context;
        }

        // Check cache first - use thread-safe operations
        auto cachedContextOpt = m_combatContextCache.Find(formID);
        if (cachedContextOpt.has_value()) {
            // Check if cache is still valid
            const auto &cached = cachedContextOpt.value();
            if (a_currentTime - cached.lastUpdateTime < COMBAT_CONTEXT_UPDATE_INTERVAL) {
                context = cached.context;
                // Ensure raw pointer is cleared (it was cleared when cached)
                context.closestEnemy = nullptr;
                // Refresh the distance/range from the live target - the cached scan
                // data (enemy/ally counts) is fine to reuse, but range must be current.
                UpdateRangeCategory(a_actor, context, a_weaponReach);
                return context;
            }
        }

        // Not in cache or expired, gather fresh data
        RE::CombatController *combatController = ActorUtils::SafeGetCombatController(a_actor);

        if (!combatController) {
            return context;
        }

        // Get primary target
        RE::ActorHandle primaryTarget = combatController->targetHandle;
        RE::NiPointer<RE::Actor> target = primaryTarget.get();
        if (target && target.get()) {
            // Use safe position access to prevent crashes
            auto selfPosOpt = ActorUtils::SafeGetPosition(a_actor);
            auto targetPosOpt = ActorUtils::SafeGetPosition(target.get());
            if (selfPosOpt.has_value() && targetPosOpt.has_value()) {
                context.enemyCount = 1;
                // Don't store raw pointer - it becomes invalid
                context.closestEnemy = nullptr;
                context.closestEnemyDistance =
                    StateHelpers::CalculateDistance(selfPosOpt.value(), targetPosOpt.value());
            }
        }

        // Scan for nearby actors (enemies and allies) - expensive operation, done
        // in one pass
        ScanForNearbyActors(a_actor, context, target.get());

        // Range granularity is computed from the live target position (never
        // cached) so it can't lag behind the actor's real distance.
        UpdateRangeCategory(a_actor, context, a_weaponReach);

        // Calculate threat level based on enemy count
        if (context.enemyCount == 0) {
            context.threatLevel = ThreatLevel::None;
        } else if (context.enemyCount == 1) {
            context.threatLevel = ThreatLevel::Low;
        } else if (context.enemyCount == 2) {
            context.threatLevel = ThreatLevel::Moderate;
        } else if (context.enemyCount <= 4) {
            context.threatLevel = ThreatLevel::High;
        } else {
            context.threatLevel = ThreatLevel::Critical;
        }

        // Update cache - only if actor is still valid
        if (!ActorUtils::SafeIsDead(a_actor) && ActorUtils::SafeIsInCombat(a_actor)) {
            // Clear raw pointer before caching
            context.closestEnemy = nullptr;
            CachedCombatContext cached{context, a_currentTime};
            m_combatContextCache.Emplace(formID, std::move(cached));
        }

        return context;
    }

    void ActorStateObserver::ScanForNearbyActors(RE::Actor *a_actor, CombatContext &a_context,
                                                 RE::Actor *a_primaryTarget)
    {
        if (!a_actor) {
            return;
        }

        // Validate actor before expensive operations - use wrapper
        if (!ActorUtils::SafeIsInCombat(a_actor)) {
            return;
        }

        // Scan range for nearby actors
        const float scanRange = 2000.0f; // Game units
        auto selfPosOpt = ActorUtils::SafeGetPosition(a_actor);
        if (!selfPosOpt.has_value()) {
            return; // Position access failed
        }
        RE::NiPoint3 selfPos = selfPosOpt.value();

        int additionalEnemies = 0;
        int allies = 0;
        int enemiesTargetingUs = 0; // Count enemies that have us as their target
        float closestDistance = a_context.closestEnemyDistance > 0.0f ? a_context.closestEnemyDistance : scanRange;

        // Get self forward vector for threat assessment
        RE::NiPoint3 selfForward = StateHelpers::GetActorForwardVector(a_actor);

        // Get primary target position and forward vector for flanking calculations
        RE::NiPoint3 targetPos = RE::NiPoint3(0.0f, 0.0f, 0.0f);
        RE::NiPoint3 targetForward = RE::NiPoint3(0.0f, 1.0f, 0.0f);
        bool hasTarget = false;
        if (a_primaryTarget) {
            auto targetPosOpt = ActorUtils::SafeGetPosition(a_primaryTarget);
            if (targetPosOpt.has_value()) {
                targetPos = targetPosOpt.value();
                targetForward = StateHelpers::GetActorForwardVector(a_primaryTarget);
                hasTarget = true;
            }
        }

        // Record an ally as the flanking partner, with the target's facing relative to it
        bool sawPlayer = false;
        auto trackAlly = [&](const RE::NiPoint3 &a_allyPos, float a_distance) {
            a_context.closestAllyPosition = a_allyPos;
            a_context.closestAllyDistance = a_distance;
            a_context.hasNearbyAlly = (a_distance <= 1500.0f); // Flanking range threshold
            a_context.allyIsPlayer = false;

            if (!hasTarget) {
                return;
            }
            RE::NiPoint3 targetToAlly = a_allyPos - targetPos;
            if (targetToAlly.SqrLength() <= 0.01f) {
                return;
            }
            targetToAlly.Unitize();

            // 1.0 = target facing ally, -1.0 = target facing away from ally
            const float targetAllyDot = targetForward.Dot(targetToAlly);
            a_context.targetFacingAllyDot = targetAllyDot;
            a_context.targetFacingAwayFromAlly = (targetAllyDot < -0.3f); // good flanking opportunity
            a_context.targetFacingTowardAlly = (targetAllyDot > 0.3f);    // we can flank from behind
        };

        // Get current cell to scan actors in loaded area
        RE::TESObjectCELL *currentCell = nullptr;
        currentCell = a_actor->GetParentCell();

        if (!currentCell || !currentCell->IsAttached()) {
            return;
        }

        // Scan actors in the current cell - single pass for both enemies and allies
        auto &references = currentCell->GetRuntimeData().references;

        // Get size first to detect container modification during iteration
        size_t initialSize = references.size();
        size_t processedCount = 0;

        for (auto &ref : references) {
            processedCount++;

            // Safety check: if container size changed, abort to avoid iterator
            // invalidation
            if (references.size() != initialSize) {
                break; // Container was modified, abort scan
            }

            RE::TESObjectREFR *refr = nullptr;
            refr = ref.get();

            if (!refr) {
                continue;
            }

            RE::Actor *nearbyActor = nullptr;
            nearbyActor = refr->As<RE::Actor>();

            if (!nearbyActor) {
                continue;
            }

            // Skip self
            if (nearbyActor == a_actor) {
                continue;
            }

            // Skip primary target - it's already counted in enemyCount
            if (a_primaryTarget && nearbyActor == a_primaryTarget) {
                continue;
            }

            // Validate nearby actor before accessing properties
            // Use wrapper for safe validation
            if (ActorUtils::SafeIsDead(nearbyActor) || !ActorUtils::SafeIsInCombat(nearbyActor)) {
                continue; // Actor validation failed
            }

            // Re-validate self actor before distance calculation
            if (ActorUtils::SafeIsDead(a_actor) || !ActorUtils::SafeIsInCombat(a_actor)) {
                return; // Self became invalid, abort scan
            }

            // Check distance - use wrapper for safe position access
            auto actorPosOpt = ActorUtils::SafeGetPosition(nearbyActor);
            if (!actorPosOpt.has_value()) {
                continue; // Position access failed
            }
            RE::NiPoint3 actorPos = actorPosOpt.value();

            float distance = StateHelpers::CalculateDistance(selfPos, actorPos);
            if (distance > scanRange) {
                continue;
            }

            // Re-validate actors before hostility check (they might have become
            // invalid) Use wrapper for safe access
            if (ActorUtils::SafeIsDead(a_actor) || ActorUtils::SafeIsDead(nearbyActor) ||
                !ActorUtils::SafeIsInCombat(a_actor) || !ActorUtils::SafeIsInCombat(nearbyActor)) {
                continue; // Actor validation failed
            }

            // Determine if enemy or ally - use wrapper for safe hostility check
            // Check if nearbyActor is hostile to a_actor (not the other way around)
            // IsHostileToActor(a, b) checks if 'a' is hostile to 'b'
            // So we want: is nearbyActor hostile to a_actor? ->
            // IsHostileToActor(nearbyActor, a_actor)
            bool isHostile = ActorUtils::SafeIsHostileToActor(nearbyActor, a_actor);

            // Re-validate actors one more time before using distance/position data
            // Actors can become invalid during the loop
            if (ActorUtils::SafeIsDead(nearbyActor) || !ActorUtils::SafeIsInCombat(nearbyActor)) {
                continue; // Actor became invalid, skip
            }

            if (isHostile) {
                // Found an additional enemy
                additionalEnemies++;

                // Check if this enemy is targeting us (has us as their combat target)
                // This helps determine threat level
                RE::CombatController *enemyCombatController =
                            ActorUtils::SafeGetCombatController(nearbyActor);
                        if (enemyCombatController) {
                    RE::ActorHandle enemyTargetHandle = enemyCombatController->targetHandle;
                    RE::NiPointer<RE::Actor> enemyTarget = enemyTargetHandle.get();
                    if (enemyTarget && enemyTarget.get() == a_actor) {
                        enemiesTargetingUs++; // This enemy is targeting us
                    }
                }

                // Track the weakest secondary enemy (target switching finisher)
                const float enemyHealth = GetActorValuePercent(nearbyActor, RE::ActorValue::kHealth);
                if (enemyHealth < a_context.weakestEnemyHealth) {
                    a_context.weakestEnemy = nearbyActor->GetHandle();
                    a_context.weakestEnemyHealth = enemyHealth;
                    a_context.weakestEnemyDistance = distance;
                }

                // Update closest enemy if this one is closer
                // Don't store raw pointer - it becomes invalid
                if (distance < closestDistance) {
                    closestDistance = distance;
                    a_context.closestEnemy = nullptr; // Don't store raw pointer
                    a_context.closestEnemyDistance = distance;
                }
            } else {
                // Found an ally (non-hostile actor in combat)
                allies++;

                sawPlayer = sawPlayer || nearbyActor->IsPlayerRef();

                // Track closest ally position for flanking calculations
                // Only update if this ally is closer than previous closest
                if (!a_context.hasNearbyAlly || distance < a_context.closestAllyDistance) {
                    trackAlly(actorPos, distance);
                }
            }
        }

        // Followers pair up with the player: the player becomes their flanking partner
        // so EvaluateFlanking sets up a pincer around whatever the player is fighting.
        if (Config::GetInstance().GetTeamwork().enableFollowerPincer && a_actor->IsPlayerTeammate()) {
            auto *player = RE::PlayerCharacter::GetSingleton();
            if (player && player->IsInCombat() && !player->IsDead()) {
                const RE::NiPoint3 playerPos = player->GetPosition();
                const float distance = StateHelpers::CalculateDistance(selfPos, playerPos);
                if (distance <= scanRange) {
                    if (!sawPlayer) {
                        allies++;
                    }
                    trackAlly(playerPos, distance);
                    a_context.allyIsPlayer = true;
                }
            }
        }

        // Update counts
        a_context.enemyCount += additionalEnemies;
        a_context.allyCount = allies;
        a_context.enemiesTargetingUs = enemiesTargetingUs;

        // If we have a primary target, they're also targeting us (count them)
        if (a_primaryTarget && a_context.enemyCount > 0) {
            a_context.enemiesTargetingUs++; // Primary target is targeting us
        }
    }

    TemporalState ActorStateObserver::GatherTemporalState(RE::Actor *a_actor, RE::Actor *a_target, float a_deltaTime,
                                                          const ActorStateData &a_state)
    {
        TemporalState temporal;

        if (!a_actor) {
            return temporal;
        }

        auto formIDOpt = ActorUtils::SafeGetFormID(a_actor);
        if (!formIDOpt.has_value()) {
            return temporal;
        }
        RE::FormID formID = formIDOpt.value();

        // Get or create actor temporal data and update it atomically under the map's
        // lock. Holding the lock for the whole read-modify-write keeps the reference
        // valid even if another thread inserts or erases entries concurrently.
        m_actorTemporalData.ModifyOrCreate(formID, [&](ActorTemporalData &actorTemporal) {
            // Update timers
            actorTemporal.timeSinceLastAttack += a_deltaTime;
            actorTemporal.timeSinceLastDodge += a_deltaTime;
            actorTemporal.timeSinceLastEvasion += a_deltaTime;
            actorTemporal.timeSinceLastAction += a_deltaTime;
            actorTemporal.timeSinceLastPowerAttack += a_deltaTime;
            actorTemporal.timeSinceLastSprintAttack += a_deltaTime;
            actorTemporal.timeSinceLastBash += a_deltaTime;
            actorTemporal.timeSinceLastFeint += a_deltaTime;

            // Track duration of current states
            bool isBlocking = ActorUtils::SafeIsBlocking(a_actor);
            bool isAttacking = ActorUtils::SafeIsAttacking(a_actor);
            RE::ATTACK_STATE_ENUM attackState = ActorUtils::SafeGetAttackState(a_actor);
            bool isIdle = (attackState == RE::ATTACK_STATE_ENUM::kNone);

            if (isBlocking) {
                if (actorTemporal.wasBlocking) {
                    actorTemporal.blockingDuration += a_deltaTime;
                } else {
                    actorTemporal.blockingDuration = a_deltaTime; // Just started blocking
                }
            } else {
                actorTemporal.blockingDuration = 0.0f;
            }

            if (isAttacking || attackState != RE::ATTACK_STATE_ENUM::kNone) {
                if (actorTemporal.wasAttacking || actorTemporal.previousAttackState != RE::ATTACK_STATE_ENUM::kNone) {
                    actorTemporal.attackingDuration += a_deltaTime;
                } else {
                    actorTemporal.attackingDuration = a_deltaTime; // Just started attacking
                    // Track attack attempt for defense feedback
                    if (a_target) {
                        bool isPowerAttack = IsPowerAttacking(a_actor);
                        AttackDefenseFeedbackTracker::GetInstance().RecordAttackAttempt(
                            a_actor, a_target, isPowerAttack, a_state.weaponReach, a_state.weaponReachFromPrecision,
                            a_state.target.distance, a_state.target.boundRadius,
                            StateHelpers::EffectiveAttackRange(
                                a_state.weaponReach, a_state.target.boundRadius,
                                Config::GetInstance().GetDecisionMatrix().offenseReachMultiplier,
                                Config::GetInstance().GetDecisionMatrix().offenseReachSafetyMargin));
                    }
                }
            } else {
                actorTemporal.attackingDuration = 0.0f;
            }

            if (isIdle) {
                if (actorTemporal.wasIdle) {
                    actorTemporal.idleDuration += a_deltaTime;
                } else {
                    actorTemporal.idleDuration = a_deltaTime; // Just became idle
                }
            } else {
                actorTemporal.idleDuration = 0.0f;
            }

            // Update previous states
            actorTemporal.wasBlocking = isBlocking;
            actorTemporal.wasAttacking = isAttacking;
            actorTemporal.wasIdle = isIdle;
            actorTemporal.previousAttackState = attackState;

            // Copy to output
            temporal.self.timeSinceLastAttack = actorTemporal.timeSinceLastAttack;
            temporal.self.timeSinceLastDodge = actorTemporal.timeSinceLastDodge;
            temporal.self.timeSinceLastEvasion = actorTemporal.timeSinceLastEvasion;
            temporal.self.timeSinceLastAction = actorTemporal.timeSinceLastAction;
            temporal.self.blockingDuration = actorTemporal.blockingDuration;
            temporal.self.attackingDuration = actorTemporal.attackingDuration;
            temporal.self.idleDuration = actorTemporal.idleDuration;
            temporal.self.timeSinceLastPowerAttack = actorTemporal.timeSinceLastPowerAttack;
            temporal.self.timeSinceLastSprintAttack = actorTemporal.timeSinceLastSprintAttack;
            temporal.self.timeSinceLastBash = actorTemporal.timeSinceLastBash;
            temporal.self.timeSinceLastFeint = actorTemporal.timeSinceLastFeint;

            // Copy parry feedback
            temporal.self.lastParrySuccess = actorTemporal.lastParrySuccess;
            temporal.self.lastParryEstimatedDuration = actorTemporal.lastParryEstimatedDuration;
            temporal.self.timeSinceLastParryAttempt = actorTemporal.timeSinceLastParryAttempt;
            temporal.self.parrySuccessCount = actorTemporal.parrySuccessCount;
            temporal.self.parryAttemptCount = actorTemporal.parryAttemptCount;

            // Copy timed block feedback
            temporal.self.lastTimedBlockSuccess = actorTemporal.lastTimedBlockSuccess;
            temporal.self.lastTimedBlockEstimatedDuration = actorTemporal.lastTimedBlockEstimatedDuration;
            temporal.self.timeSinceLastTimedBlockAttempt = actorTemporal.timeSinceLastTimedBlockAttempt;
            temporal.self.timedBlockSuccessCount = actorTemporal.timedBlockSuccessCount;
            temporal.self.timedBlockAttemptCount = actorTemporal.timedBlockAttemptCount;

            // Copy attack defense feedback
            temporal.self.lastAttackParried = actorTemporal.lastAttackParried;
            temporal.self.lastAttackTimedBlocked = actorTemporal.lastAttackTimedBlocked;
            temporal.self.lastAttackHit = actorTemporal.lastAttackHit;
            temporal.self.lastAttackMissed = actorTemporal.lastAttackMissed;
            temporal.self.timeSinceLastParriedAttack = actorTemporal.timeSinceLastParriedAttack;
            temporal.self.timeSinceLastTimedBlockedAttack = actorTemporal.timeSinceLastTimedBlockedAttack;
            temporal.self.timeSinceLastHitAttack = actorTemporal.timeSinceLastHitAttack;
            temporal.self.timeSinceLastMissedAttack = actorTemporal.timeSinceLastMissedAttack;
            temporal.self.parriedAttackCount = actorTemporal.parriedAttackCount;
            temporal.self.timedBlockedAttackCount = actorTemporal.timedBlockedAttackCount;
            temporal.self.hitAttackCount = actorTemporal.hitAttackCount;
            temporal.self.missedAttackCount = actorTemporal.missedAttackCount;
            temporal.self.totalAttackCount = actorTemporal.totalAttackCount;
            temporal.self.parryRate = actorTemporal.parryRate;
            temporal.self.timedBlockRate = actorTemporal.timedBlockRate;
            temporal.self.hitRate = actorTemporal.hitRate;
            temporal.self.missRate = actorTemporal.missRate;
            temporal.self.totalDefenseRate = actorTemporal.totalDefenseRate;
            temporal.self.recentDamageFraction = actorTemporal.recentDamageFraction;
            temporal.self.hasMomentumData = actorTemporal.hasMomentumData;

            // Update parry feedback from ParryFeedbackTracker
            auto parryFeedback = ParryFeedbackTracker::GetInstance().GetFeedback(a_actor);
            actorTemporal.lastParrySuccess = parryFeedback.lastParrySuccess;
            actorTemporal.lastParryEstimatedDuration = parryFeedback.lastParryEstimatedDuration;
            actorTemporal.timeSinceLastParryAttempt = parryFeedback.timeSinceLastParryAttempt;
            actorTemporal.parrySuccessCount = parryFeedback.parrySuccessCount;
            actorTemporal.parryAttemptCount = parryFeedback.parryAttemptCount;

            // Update timed block feedback from TimedBlockFeedbackTracker
            auto timedBlockFeedback = TimedBlockFeedbackTracker::GetInstance().GetFeedback(a_actor);
            actorTemporal.lastTimedBlockSuccess = timedBlockFeedback.lastTimedBlockSuccess;
            actorTemporal.lastTimedBlockEstimatedDuration = timedBlockFeedback.lastTimedBlockEstimatedDuration;
            actorTemporal.timeSinceLastTimedBlockAttempt = timedBlockFeedback.timeSinceLastTimedBlockAttempt;
            actorTemporal.timedBlockSuccessCount = timedBlockFeedback.timedBlockSuccessCount;
            actorTemporal.timedBlockAttemptCount = timedBlockFeedback.timedBlockAttemptCount;

            // Update guard counter feedback from GuardCounterFeedbackTracker
            auto guardCounterFeedback = GuardCounterFeedbackTracker::GetInstance().GetFeedback(a_actor);
            actorTemporal.lastGuardCounterSuccess = guardCounterFeedback.lastGuardCounterSuccess;
            actorTemporal.timeSinceLastGuardCounterAttempt = guardCounterFeedback.timeSinceLastGuardCounterAttempt;
            actorTemporal.guardCounterSuccessCount = guardCounterFeedback.guardCounterSuccessCount;
            actorTemporal.guardCounterAttemptCount = guardCounterFeedback.guardCounterAttemptCount;
            actorTemporal.guardCounterFailedCount = guardCounterFeedback.guardCounterFailedCount;
            actorTemporal.guardCounterMissedOpportunityCount = guardCounterFeedback.guardCounterMissedOpportunityCount;
            actorTemporal.guardCounterSuccessRate = guardCounterFeedback.guardCounterSuccessRate;

            // Update attack defense feedback from AttackDefenseFeedbackTracker
            auto attackDefenseFeedback = AttackDefenseFeedbackTracker::GetInstance().GetFeedback(a_actor);
            actorTemporal.lastAttackParried = attackDefenseFeedback.lastAttackParried;
            actorTemporal.lastAttackTimedBlocked = attackDefenseFeedback.lastAttackTimedBlocked;
            actorTemporal.lastAttackHit = attackDefenseFeedback.lastAttackHit;
            actorTemporal.lastAttackMissed = attackDefenseFeedback.lastAttackMissed;
            actorTemporal.timeSinceLastParriedAttack = attackDefenseFeedback.timeSinceLastParriedAttack;
            actorTemporal.timeSinceLastTimedBlockedAttack = attackDefenseFeedback.timeSinceLastTimedBlockedAttack;
            actorTemporal.timeSinceLastHitAttack = attackDefenseFeedback.timeSinceLastHitAttack;
            actorTemporal.timeSinceLastMissedAttack = attackDefenseFeedback.timeSinceLastMissedAttack;
            actorTemporal.parriedAttackCount = attackDefenseFeedback.parriedAttackCount;
            actorTemporal.timedBlockedAttackCount = attackDefenseFeedback.timedBlockedAttackCount;
            actorTemporal.hitAttackCount = attackDefenseFeedback.hitAttackCount;
            actorTemporal.missedAttackCount = attackDefenseFeedback.missedAttackCount;
            actorTemporal.totalAttackCount = attackDefenseFeedback.totalAttackCount;
            actorTemporal.parryRate = attackDefenseFeedback.parryRate;
            actorTemporal.timedBlockRate = attackDefenseFeedback.timedBlockRate;
            actorTemporal.hitRate = attackDefenseFeedback.hitRate;
            actorTemporal.missRate = attackDefenseFeedback.missRate;
            actorTemporal.totalDefenseRate = attackDefenseFeedback.totalDefenseRate;
            actorTemporal.recentDamageFraction = attackDefenseFeedback.recentDamageFraction;
            actorTemporal.hasMomentumData = attackDefenseFeedback.hasMomentumData;
        });

        // Damage race inputs: both directions between this actor and its target
        if (a_target) {
            const auto &tracker = AttackDefenseFeedbackTracker::GetInstance();
            temporal.self.ourAverageHit = tracker.AverageHit(formID, a_target->GetFormID());
            temporal.self.theirAverageHit = tracker.AverageHit(a_target->GetFormID(), formID);
        }

        // Track target temporal state
        if (a_target) {
            auto targetFormIDOpt = ActorUtils::SafeGetFormID(a_target);
            if (targetFormIDOpt.has_value()) {
                RE::FormID targetFormID = targetFormIDOpt.value();

                // Keep one target history per observing actor; targetFormID below resets
                // it when the actor switches opponents.
                RE::FormID targetKey = formID;

                // Update the target temporal data atomically under the map's lock.
                m_targetTemporalData.ModifyOrCreate(targetKey, [&](TargetTemporalData &targetTemporal) {
                    // The cache is keyed by the observing actor, so reset it when that
                    // actor switches opponents instead of carrying habits across targets.
                    if (targetTemporal.targetFormID != targetFormID) {
                        targetTemporal = TargetTemporalData{};
                        targetTemporal.targetFormID = targetFormID;
                    }

                    // Update timers
                    targetTemporal.timeSinceLastAttack += a_deltaTime;
                    targetTemporal.timeSinceLastPowerAttack += a_deltaTime;

                    // Track duration of target states
                    bool targetIsBlocking = ActorUtils::SafeIsBlocking(a_target);
                    bool targetIsAttacking = ActorUtils::SafeIsAttacking(a_target);
                    bool targetIsCasting = (ActorUtils::SafeWhoIsCasting(a_target) != 0);
                    bool targetIsDrawing = false;
                    auto targetEquippedWeapon = ActorUtils::SafeGetEquippedObject(a_target, false);
                    if (targetEquippedWeapon && targetEquippedWeapon->IsWeapon()) {
                        auto weapon = targetEquippedWeapon->As<RE::TESObjectWEAP>();
                        if (weapon && (weapon->IsBow() || weapon->IsCrossbow())) {
                            RE::ATTACK_STATE_ENUM targetAttackState = ActorUtils::SafeGetAttackState(a_target);
                            targetIsDrawing = (targetAttackState == RE::ATTACK_STATE_ENUM::kDraw);
                        }
                    }
                    RE::ATTACK_STATE_ENUM targetAttackState = ActorUtils::SafeGetAttackState(a_target);
                    bool targetIsIdle = (targetAttackState == RE::ATTACK_STATE_ENUM::kNone);

                    // Keep recent block starts as a decaying habit score. Two recent
                    // block commitments can justify baiting a response with a feint.
                    targetTemporal.recentBlockCommitments =
                        (std::max)(0.0f, targetTemporal.recentBlockCommitments - (std::max)(0.0f, a_deltaTime) / 6.0f);
                    if (targetIsBlocking && !targetTemporal.wasBlocking) {
                        targetTemporal.recentBlockCommitments =
                            (std::min)(3.0f, targetTemporal.recentBlockCommitments + 1.0f);
                    }

                    if (targetIsBlocking) {
                        if (targetTemporal.wasBlocking) {
                            targetTemporal.blockingDuration += a_deltaTime;
                        } else {
                            targetTemporal.blockingDuration = a_deltaTime;
                        }
                    } else {
                        targetTemporal.blockingDuration = 0.0f;
                    }

                    if (targetIsAttacking || targetAttackState != RE::ATTACK_STATE_ENUM::kNone) {
                        if (targetTemporal.wasAttacking ||
                            targetTemporal.previousAttackState != RE::ATTACK_STATE_ENUM::kNone) {
                            targetTemporal.attackingDuration += a_deltaTime;
                        } else {
                            targetTemporal.attackingDuration = a_deltaTime;
                        }
                    } else {
                        targetTemporal.attackingDuration = 0.0f;
                    }

                    if (targetIsCasting) {
                        if (targetTemporal.wasCasting) {
                            targetTemporal.castingDuration += a_deltaTime;
                        } else {
                            targetTemporal.castingDuration = a_deltaTime;
                        }
                    } else {
                        targetTemporal.castingDuration = 0.0f;
                    }

                    if (targetIsDrawing) {
                        if (targetTemporal.wasDrawing) {
                            targetTemporal.drawingDuration += a_deltaTime;
                        } else {
                            targetTemporal.drawingDuration = a_deltaTime;
                        }
                    } else {
                        targetTemporal.drawingDuration = 0.0f;
                    }

                    if (targetIsIdle) {
                        if (targetTemporal.wasIdle) {
                            targetTemporal.idleDuration += a_deltaTime;
                        } else {
                            targetTemporal.idleDuration = a_deltaTime;
                        }
                    } else {
                        targetTemporal.idleDuration = 0.0f;
                    }

                    // Detect when target attacks (state transition from not attacking to
                    // attacking)
                    if ((targetIsAttacking || targetAttackState == RE::ATTACK_STATE_ENUM::kSwing) &&
                        !targetTemporal.wasAttacking &&
                        targetTemporal.previousAttackState != RE::ATTACK_STATE_ENUM::kSwing) {
                        // Target just started attacking
                        targetTemporal.timeSinceLastAttack = 0.0f;
                        targetTemporal.attackStartTime = 0.0f; // Reset attack start time
                        ++targetTemporal.swingCount;

                        // Check if it's a power attack
                        bool isPowerAttack = IsPowerAttacking(a_target);
                        targetTemporal.isPowerAttack = isPowerAttack;

                        // Estimate attack duration
                        targetTemporal.estimatedAttackDuration = EstimateAttackDuration(a_target, isPowerAttack);
                    }

                    // Update attack timing if currently attacking
                    if (targetIsAttacking || targetAttackState == RE::ATTACK_STATE_ENUM::kSwing) {
                        if (targetTemporal.attackStartTime >= 0.0f) {
                            // Attack is in progress, update elapsed time
                            targetTemporal.attackStartTime += a_deltaTime;

                            // Calculate time until attack hits
                            // Assume attack hits at ~60% of total duration (windup phase)
                            float hitTime = targetTemporal.estimatedAttackDuration * 0.6f;
                            targetTemporal.timeUntilAttackHits = hitTime - targetTemporal.attackStartTime;

                            // Clamp to reasonable range
                            if (targetTemporal.timeUntilAttackHits < 0.0f) {
                                targetTemporal.timeUntilAttackHits = 0.0f; // Attack already hit or past hit window
                            }
                        }
                    } else {
                        // Not attacking anymore, reset timing
                        targetTemporal.attackStartTime = -1.0f;
                        targetTemporal.timeUntilAttackHits = 999.0f;
                        targetTemporal.estimatedAttackDuration = 0.0f;
                    }

                    // Detect when target power attacks
                    if (targetAttackState == RE::ATTACK_STATE_ENUM::kSwing) {
                        // Check if it's a power attack (we'd need to check attack data, but
                        // for now we'll detect on follow-through) Actually, we'll detect
                        // power attack when target finishes attack (kFollowThrough) and was
                        // swinging
                        if (targetTemporal.previousAttackState == RE::ATTACK_STATE_ENUM::kSwing &&
                            targetAttackState == RE::ATTACK_STATE_ENUM::kFollowThrough) {
                            // Might be a power attack, but we can't be sure without checking
                            // attack data For now, we'll track it when we detect a power attack
                            // state
                        }
                    }

                    // Update previous states
                    targetTemporal.wasBlocking = targetIsBlocking;
                    targetTemporal.wasAttacking = targetIsAttacking;
                    targetTemporal.wasCasting = targetIsCasting;
                    targetTemporal.wasDrawing = targetIsDrawing;
                    targetTemporal.wasIdle = targetIsIdle;
                    targetTemporal.previousAttackState = targetAttackState;

                    // Copy to output
                    temporal.target.timeSinceLastAttack = targetTemporal.timeSinceLastAttack;
                    temporal.target.blockingDuration = targetTemporal.blockingDuration;
                    temporal.target.recentBlockCommitments = targetTemporal.recentBlockCommitments;
                    temporal.target.castingDuration = targetTemporal.castingDuration;
                    temporal.target.drawingDuration = targetTemporal.drawingDuration;
                    temporal.target.attackingDuration = targetTemporal.attackingDuration;
                    temporal.target.idleDuration = targetTemporal.idleDuration;
                    temporal.target.timeSinceLastPowerAttack = targetTemporal.timeSinceLastPowerAttack;
                    temporal.target.attackStartTime = targetTemporal.attackStartTime;
                    temporal.target.swingCount = targetTemporal.swingCount;
                    temporal.target.estimatedAttackDuration = targetTemporal.estimatedAttackDuration;
                    temporal.target.timeUntilAttackHits = targetTemporal.timeUntilAttackHits;
                });
            }
        }

        return temporal;
    }

    void ActorStateObserver::NotifyActionExecuted(RE::Actor *a_actor, ActionType a_action)
    {
        if (!a_actor) {
            return;
        }

        auto formIDOpt = ActorUtils::SafeGetFormID(a_actor);
        if (!formIDOpt.has_value()) {
            return;
        }
        RE::FormID formID = formIDOpt.value();

        // Reset timers based on action type, atomically under the map's lock. If the
        // actor has no temporal entry yet, there is nothing to reset.
        m_actorTemporalData.Modify(formID, [&](ActorTemporalData &temporal) {
            // The anti-spam gate (no attack within 0.3 s of the last action) counts deliberate
            // actions only. Locomotion is re-issued every decision while it runs, so counting it
            // kept the timer at ~0 and offense was never evaluated (0 Attack candidates in 1398).
            // Feint is re-issued the same way (126 of 146 blocked in-range ticks in run 4); its
            // own timer (timeSinceLastFeint) still runs.
            switch (a_action) {
            case ActionType::Feint:
            case ActionType::Advancing:
            case ActionType::Strafe:
            case ActionType::Flanking:
            case ActionType::Retreat:
            case ActionType::Backoff:
                break;
            default:
                temporal.timeSinceLastAction = 0.0f;
                break;
            }

            switch (a_action) {
            case ActionType::Attack:
            case ActionType::PowerAttack:
            case ActionType::SprintAttack:
                temporal.timeSinceLastAttack = 0.0f;
                if (a_action == ActionType::PowerAttack) {
                    temporal.timeSinceLastPowerAttack = 0.0f;
                } else if (a_action == ActionType::SprintAttack) {
                    temporal.timeSinceLastSprintAttack = 0.0f;
                }
                break;
            case ActionType::Dodge:
            case ActionType::ForwardDodge:
            case ActionType::Jump:
            case ActionType::Strafe:
                temporal.timeSinceLastDodge = 0.0f;
                if (a_action == ActionType::Dodge || a_action == ActionType::Jump) {
                    temporal.timeSinceLastEvasion = 0.0f;
                }
                break;
            case ActionType::Bash:
                temporal.timeSinceLastBash = 0.0f;
                break;
            case ActionType::Feint:
                temporal.timeSinceLastFeint = 0.0f;
                break;
            default:
                break;
            }
        });
    }

    void ActorStateObserver::Cleanup(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return;
        }

        // Get FormID safely - use FormID as key instead of raw pointer
        auto formIDOpt = ActorUtils::SafeGetFormID(a_actor);
        if (!formIDOpt.has_value()) {
            return; // Can't get FormID, actor is invalid
        }
        RE::FormID formID = formIDOpt.value();

        // Remove cached combat context for this actor
        m_combatContextCache.Erase(formID);

        // Remove temporal data for this actor
        m_actorTemporalData.Erase(formID);
        m_targetTemporalData.Erase(formID);
    }

    void ActorStateObserver::EvictActor(RE::FormID a_formID)
    {
        if (a_formID == 0) {
            return;
        }
        m_combatContextCache.Erase(a_formID);
        m_actorTemporalData.Erase(a_formID);
        m_targetTemporalData.Erase(a_formID);
        m_detectionCache.Erase(a_formID);
    }
} // namespace CombatAI
