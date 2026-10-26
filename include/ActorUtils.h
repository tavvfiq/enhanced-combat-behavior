#pragma once

#include "pch.h"
#include <excpt.h> // SEH (__try/__except, EXCEPTION_EXECUTE_HANDLER)
#include <optional>
#include <xmmintrin.h> // _mm_store_ps (reading hkVector4)

namespace CombatAI
{
    // Null-checked wrappers for actor access with safe defaults.
    // NOTE: the plugin builds with /EHsc, so C++ try/catch does NOT catch access
    // violations from stale game pointers. Guard real fault-prone engine calls with
    // SEH (__try/__except) in a function holding no C++ objects - see
    // SEHRequestDetectionLevel below.

    namespace ActorUtils
    {
        // Check if actor is valid and accessible
        inline bool IsValid(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return false;
            }
            return !a_actor->IsDead() && a_actor->IsInCombat();
        }

        // Safe GetFormID - returns optional to handle failures
        inline std::optional<RE::FormID> SafeGetFormID(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return std::nullopt;
            }
            return a_actor->GetFormID();
        }

        // Safe GetEquippedObject
        inline RE::TESForm *SafeGetEquippedObject(RE::Actor *a_actor, bool a_leftHand)
        {
            if (!a_actor) {
                return nullptr;
            }
            return a_actor->GetEquippedObject(a_leftHand);
        }

        // Safe AsActorState
        // In CommonLibSSE, Actor inherits from ActorState, so we can cast directly
        // Since Actor IS an ActorState (inheritance), static_cast is safe
        inline RE::ActorState *SafeAsActorState(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return nullptr;
            }
            // ActorState is at a runtime-dependent base offset in SE/AE.
            return a_actor->AsActorState();
        }

        // Safe AsActorValueOwner
        // In CommonLibSSE, Actor inherits from ActorValueOwner, so we can cast directly
        inline RE::ActorValueOwner *SafeAsActorValueOwner(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return nullptr;
            }
            // ActorValueOwner is at a runtime-dependent base offset in SE/AE.
            return a_actor->AsActorValueOwner();
        }

        // Safe GetActorBase
        inline RE::TESNPC *SafeGetActorBase(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return nullptr;
            }
            return a_actor->GetActorBase();
        }

        // Safe Get3D
        inline RE::NiAVObject *SafeGet3D(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return nullptr;
            }
            return a_actor->Get3D();
        }

        // Horizontal body radius (game units) from the actor's bounding box.
        // Used to pad melee reach so center-to-center distance checks account for
        // the physical size of the actor. Falls back to a humanoid default.
        inline float GetBodyRadius(RE::Actor *a_actor)
        {
            constexpr float kDefaultRadius = 30.0f;
            constexpr float kMinRadius = 24.0f;
            if (!a_actor) {
                return kDefaultRadius;
            }
            RE::NiPoint3 boundMax = a_actor->GetBoundMax();
            RE::NiPoint3 boundMin = a_actor->GetBoundMin();
            float halfX = (boundMax.x - boundMin.x) * 0.5f;
            float halfY = (boundMax.y - boundMin.y) * 0.5f;
            float radius = (halfX > halfY) ? halfX : halfY;
            if (radius < kMinRadius) {
                return kMinRadius;
            }
            return radius;
        }

        // Safe WhoIsCasting
        inline std::uint32_t SafeWhoIsCasting(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return 0;
            }
            return a_actor->WhoIsCasting();
        }

        // Safe SetGraphVariableBool
        inline bool SafeSetGraphVariableBool(RE::Actor *a_actor, const char *a_varName, bool a_value)
        {
            if (!a_actor) {
                return false;
            }
            return a_actor->SetGraphVariableBool(a_varName, a_value);
        }

        // Safe SetGraphVariableFloat
        inline bool SafeSetGraphVariableFloat(RE::Actor *a_actor, const char *a_varName, float a_value)
        {
            if (!a_actor) {
                return false;
            }
            return a_actor->SetGraphVariableFloat(a_varName, a_value);
        }

        // Safe SetGraphVariableInt
        inline bool SafeSetGraphVariableInt(RE::Actor *a_actor, const char *a_varName, int32_t a_value)
        {
            if (!a_actor) {
                return false;
            }
            return a_actor->SetGraphVariableInt(a_varName, a_value);
        }

        // Safe GetGraphVariableBool
        inline bool SafeGetGraphVariableBool(RE::Actor *a_actor, const char *a_varName, bool &a_outValue)
        {
            if (!a_actor) {
                return false;
            }
            return a_actor->GetGraphVariableBool(a_varName, a_outValue);
        }

        // Safe NotifyAnimationGraph
        inline bool SafeNotifyAnimationGraph(RE::Actor *a_actor, const char *a_eventName)
        {
            if (!a_actor) {
                return false;
            }
            RE::BSFixedString eventName(a_eventName);
            return a_actor->NotifyAnimationGraph(eventName);
        }

        // Safe GetAttackState
        inline RE::ATTACK_STATE_ENUM SafeGetAttackState(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return RE::ATTACK_STATE_ENUM::kNone;
            }
            RE::ActorState *state = SafeAsActorState(a_actor);
                if (state) {
                return state->GetAttackState();
            }
            return RE::ATTACK_STATE_ENUM::kNone;
        }

        // Safe IsSprinting
        inline bool SafeIsSprinting(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return false;
            }
            RE::ActorState *state = SafeAsActorState(a_actor);
                if (state) {
                return state->IsSprinting();
            }
            return false;
        }

        // Safe SetSprinting
        inline void SafeSetSprinting(RE::Actor *a_actor, bool a_sprinting)
        {
            if (!a_actor) {
                return;
            }
            RE::ActorState *state = SafeAsActorState(a_actor);
                if (state) {
                state->actorState1.sprinting = a_sprinting ? 1 : 0;
            }
        }

        // Safe GetPosition
        inline std::optional<RE::NiPoint3> SafeGetPosition(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return std::nullopt;
            }
            return a_actor->GetPosition();
        }

        // Safe IsDead
        inline bool SafeIsDead(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return true; // Consider null as dead
            }
            return a_actor->IsDead();
        }

        // Safe IsInCombat
        inline bool SafeIsInCombat(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return false;
            }
            return a_actor->IsInCombat();
        }

        // Safe IsHostileToActor
        inline bool SafeIsHostileToActor(RE::Actor *a_actor, RE::Actor *a_target)
        {
            if (!a_actor || !a_target) {
                return false;
            }
            return a_actor->IsHostileToActor(a_target);
        }

        // RequestDetectionLevel runs the game's detection calc, which walks actor/process
        // data and can access transient/freed pointers under heavy load - a hardware
        // access violation that a C++ try/catch does NOT catch. Guard it with SEH so a
        // fault returns a safe default instead of crashing. Kept in its own function with
        // no C++ objects (required to mix __try/__except with the rest of the codebase).
        inline std::int32_t SEHRequestDetectionLevel(RE::Actor *a_observer, RE::Actor *a_target)
        {
            __try {
                return a_observer->RequestDetectionLevel(a_target);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return -1; // sentinel: detection calc faulted
            }
        }

        inline std::int32_t SafeRequestDetectionLevel(RE::Actor *a_observer, RE::Actor *a_target)
        {
            if (!a_observer || !a_target) {
                return 0;
            }
            std::int32_t result = SEHRequestDetectionLevel(a_observer, a_target);
            return (result < 0) ? 0 : result;
        }

        // Safe GetLevel
        inline std::uint16_t SafeGetLevel(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return 1; // Default level
            }
            return a_actor->GetLevel();
        }

        // Safe IsPlayerRef
        inline bool SafeIsPlayerRef(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return false;
            }
            return a_actor->IsPlayerRef();
        }

        // Liveness guards. An actor can be in combat yet mid-teardown (3D unloaded,
        // deleted, or disabled), in which case touching its process/knowledge data in the
        // heavier gather path can hit transient/freed pointers. Treat null / faulted as
        // "not safe to process" (deleted/disabled default true, 3D-loaded default false).
        inline bool SafeIs3DLoaded(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return false;
            }
            return a_actor->Is3DLoaded();
        }

        inline bool SafeIsDeleted(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return true;
            }
            return a_actor->IsDeleted();
        }

        inline bool SafeIsDisabled(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return true;
            }
            return a_actor->IsDisabled();
        }

        // Safe HasKeywordString
        inline bool SafeHasKeywordString(RE::Actor *a_actor, const char *a_keyword)
        {
            if (!a_actor || !a_keyword) {
                return false;
            }
            return a_actor->HasKeywordString(a_keyword);
        }

        // Safe CalculateCachedOwnerIsNPC
        inline bool SafeCalculateCachedOwnerIsNPC(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return false;
            }
            return a_actor->CalculateCachedOwnerIsNPC();
        }

        // Safe IsAIEnabled
        inline bool SafeIsAIEnabled(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return false;
            }
            return a_actor->IsAIEnabled();
        }

        // Safe HasMagicEffect - Check if actor has a specific magic effect active
        inline bool SafeHasMagicEffect(RE::Actor *a_actor, RE::EffectSetting *a_effect)
        {
            if (!a_actor || !a_effect) {
                return false;
            }
            auto magicTarget = a_actor->GetMagicTarget();
            if (!magicTarget) {
                return false;
            }
            return magicTarget->HasMagicEffect(a_effect);
        }

        // Safe IsBlocking
        inline bool SafeIsBlocking(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return false;
            }
            return a_actor->IsBlocking();
        }

        // Safe IsAttacking
        inline bool SafeIsAttacking(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return false;
            }
            return a_actor->IsAttacking();
        }

        // Safe IsWalking
        inline bool SafeIsWalking(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return false;
            }
            RE::ActorState *state = SafeAsActorState(a_actor);
            if (state) {
                return state->IsWalking();
            }
            return false;
        }

        // Safe GetKnockState
        inline RE::KNOCK_STATE_ENUM SafeGetKnockState(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return RE::KNOCK_STATE_ENUM::kNormal;
            }
            RE::ActorState *state = SafeAsActorState(a_actor);
            if (state) {
                return state->GetKnockState();
            }
            return RE::KNOCK_STATE_ENUM::kNormal;
        }

        // Safe GetCombatController (runtime data is version-dependent in NG)
        inline RE::CombatController *SafeGetCombatController(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return nullptr;
            }
            return a_actor->GetActorRuntimeData().combatController;
        }

        // Safe IsFleeing (from CombatController)
        inline bool SafeIsFleeing(RE::Actor *a_actor)
        {
            if (!a_actor) {
                return false;
            }
            RE::CombatController *combatController = SafeGetCombatController(a_actor);
            if (combatController) {
                return combatController->IsFleeing();
            }
            return false;
        }

        // Safe GetHeading
        inline std::optional<float> SafeGetHeading(RE::Actor *a_actor, bool a_absolute)
        {
            if (!a_actor) {
                return std::nullopt;
            }
            return a_actor->GetHeading(a_absolute);
        }

        // Direct physics control: set the controller's horizontal velocity.
        // a_velocity is in game units/s (Z ignored); the controller takes Havok units/s
        // (game units * world scale, ~1/70). The current vertical speed is kept so this
        // never cancels gravity, falling, or a jump in progress.
        inline void SafeApplyHorizontalVelocity(RE::Actor *a_actor, const RE::NiPoint3 &a_velocity)
        {
            if (!a_actor) {
                return;
            }
            RE::bhkCharacterController *charController = a_actor->GetCharController();
            if (!charController) {
                return;
            }
            RE::hkVector4 current{};
            charController->GetLinearVelocityImpl(current);
            alignas(16) float v[4]{};
            _mm_store_ps(v, current.quad);
            const float vz = std::isfinite(v[2]) ? v[2] : 0.0f;
            const float scale = RE::bhkWorld::GetWorldScale();
            charController->SetLinearVelocityImpl(RE::hkVector4(a_velocity.x * scale, a_velocity.y * scale, vz, 0.0f));
        }
        // A creature that fights like an NPC: tagged ActorTypeCreature but carrying a real weapon (draugr, falmer,
    // skeletons). Giants and dragon priests stay out (Skyrim.esm GiantRace 0x131F9, C00GiantOutsideWhiterunRace
    // 0xCAE13, DragonPriestRace 0x131EF; FormIDs from a read-only scan of the race records). No dodge animations
    // exist for them, so the brain never dodges with one.
    inline bool IsHumanoidCreature(RE::Actor *a_actor)
    {
        if (!a_actor || !SafeHasKeywordString(a_actor, "ActorTypeCreature")) {
            return false;
        }
        const auto *race = a_actor->GetRace();
        const RE::FormID raceID = race ? race->GetFormID() : 0;
        if (raceID == 0x000131F9 || raceID == 0x000CAE13 || raceID == 0x000131EF) {
            return false;
        }
        auto *object = a_actor->GetEquippedObject(false);
        const auto *weapon = object ? object->As<RE::TESObjectWEAP>() : nullptr;
        return weapon && weapon->GetWeaponType() != RE::WEAPON_TYPE::kHandToHandMelee;
    }
} // namespace ActorUtils
} // namespace CombatAI
