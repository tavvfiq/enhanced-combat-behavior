#pragma once

#include "pch.h"

namespace CombatAI
{
    // Integration with Precision mod for weapon reach
    class PrecisionIntegration
    {
      public:
        static PrecisionIntegration &GetInstance()
        {
            static PrecisionIntegration instance;
            return instance;
        }

        PrecisionIntegration(const PrecisionIntegration &) = delete;
        PrecisionIntegration &operator=(const PrecisionIntegration &) = delete;

        // Initialize Precision API
        bool Initialize();

        // Get weapon reach for an actor (uses Precision if available, fallback otherwise)
        float GetWeaponReach(RE::Actor *a_actor, bool *a_fromPrecision = nullptr);

        // Check if Precision is available
        bool IsAvailable() const { return m_precisionAPI != nullptr; }

      private:
        PrecisionIntegration() = default;
        ~PrecisionIntegration() = default;

        // Precision API interface
        void *m_precisionAPI = nullptr;

        // Fallback: Get weapon reach without Precision
        float GetWeaponReachFallback(RE::Actor *a_actor);
    };
} // namespace CombatAI
