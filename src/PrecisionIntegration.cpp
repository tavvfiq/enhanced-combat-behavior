#include "PrecisionIntegration.h"
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
                float reach = precision->GetAttackCollisionCapsuleLength(
                    actorHandle, PRECISION_API::RequestedAttackCollisionType::Default);
                if (reach > 0.0f) {
                    if (a_fromPrecision) {
                        *a_fromPrecision = true;
                    }
                    return reach;
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

        // Get equipped weapon
        RE::TESForm *weapon = a_actor->GetEquippedObject(false); // Right hand
        if (!weapon) {
            weapon = a_actor->GetEquippedObject(true); // Left hand
        }

        if (weapon) {
            RE::TESObjectWEAP *weap = weapon->As<RE::TESObjectWEAP>();
            if (weap) {
                // weaponData.reach is a multiplier (~1.0-1.3), NOT game units.
                // Convert to game units the same way ActorStateObserver::GetWeaponReach does.
                float reachMultiplier = weap->GetReach();
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
