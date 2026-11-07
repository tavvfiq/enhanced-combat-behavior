#include "PrecisionIntegration.h"
#include "ActorUtils.h"
#include "Guns.h"
#include "Logger.h"
#include "PrecisionAPI.h"
#include "pch.h"
#include <Windows.h>

namespace CombatAI
{
    bool PrecisionIntegration::Initialize()
    {
        // Request Precision API
        m_precisionAPI = PRECISION_API::RequestPluginAPI(PRECISION_API::InterfaceVersion::V4);

        if (m_precisionAPI) {
            LOG_INFO("Precision API initialized successfully");
            return true;
        } else {
            LOG_INFO("Precision not found, using fallback weapon reach calculation");
            return false;
        }
    }

    // The weapon in the left hand that counts for melee reach: a melee weapon that is not a gun (GunKeywords).
    // A dual wielder reaches with its longer weapon; a gun in one hand with a blade in the other reaches with the
    // blade (a pistol's short mesh would otherwise set the reach and keep it from ever being close enough to attack).
    static RE::TESObjectWEAP *LeftMeleeWeapon(RE::Actor *a_actor, const Guns::Hands &a_hands)
    {
        RE::TESForm *left = ActorUtils::SafeGetEquippedObject(a_actor, true);
        auto *weapon = left ? left->As<RE::TESObjectWEAP>() : nullptr;
        return weapon && weapon->IsMelee() && !a_hands.left ? weapon : nullptr;
    }

    float PrecisionIntegration::GetWeaponReach(RE::Actor *a_actor, bool *a_fromPrecision)
    {
        if (a_fromPrecision) {
            *a_fromPrecision = false;
        }
        if (!a_actor || !a_actor->Is3DLoaded()) {
            return 150.0f; // Default fallback
        }

        // Try Precision API first
        if (m_precisionAPI) {
            auto *precision = static_cast<PRECISION_API::IVPrecision4 *>(m_precisionAPI);
            if (precision) {
                RE::ActorHandle actorHandle = a_actor->GetHandle();
                const auto hands = Guns::Of(a_actor);
                const bool rightIsGunBesideBlade = hands.right && hands.meleeOther;
                float reach = rightIsGunBesideBlade ? 0.0f
                                                    : precision->GetAttackCollisionCapsuleLength(
                                                          actorHandle, PRECISION_API::RequestedAttackCollisionType::Default);
                if (LeftMeleeWeapon(a_actor, hands)) {
                    reach = (std::max)(reach, precision->GetAttackCollisionCapsuleLength(
                                                  actorHandle, PRECISION_API::RequestedAttackCollisionType::LeftWeapon));
                }
                if (reach > 0.0f) {
                    if (a_fromPrecision) {
                        *a_fromPrecision = true;
                    }
                    // The capsule is the blade alone; it starts at the hand, about one body radius out
                    // from the actor's centre, which is where every distance here is measured from.
                    return reach + ActorUtils::GetBodyRadius(a_actor);
                }
            }
        }

        // Fallback to manual calculation
        return GetWeaponReachFallback(a_actor);
    }

    float PrecisionIntegration::GetWeaponReachFallback(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return 150.0f;
        }

        // Equipped weapon: the right hand's unless it is a gun beside a blade; a melee left weapon counts when longer
        const auto hands = Guns::Of(a_actor);
        RE::TESForm *weapon = hands.right && hands.meleeOther ? nullptr : a_actor->GetEquippedObject(false);
        if (!weapon) {
            weapon = a_actor->GetEquippedObject(true); // Left hand
        }

        if (weapon) {
            RE::TESObjectWEAP *weap = weapon->As<RE::TESObjectWEAP>();
            if (weap) {
                // weaponData.reach is a multiplier (~1.0-1.3), NOT game units.
                // Convert to game units the same way ActorStateObserver::GetWeaponReach does.
                float reachMultiplier = weap->GetReach();
                if (auto *left = LeftMeleeWeapon(a_actor, hands)) {
                    reachMultiplier = (std::max)(reachMultiplier, left->GetReach());
                }
                if (reachMultiplier > 0.0f) {
                    return reachMultiplier * 100.0f;
                }
            }
        }

        // Unarmed - get from race
        RE::TESRace *race = a_actor->GetRace();
        if (race) {
            float unarmedReach = race->data.unarmedReach;
            if (unarmedReach > 0.0f) {
                return unarmedReach;
            }
        }

        // Ultimate fallback
        return 150.0f;
    }
} // namespace CombatAI
