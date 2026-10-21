#pragma once

#include "ThreadSafeMap.h"
#include "pch.h"
#include <atomic>

namespace CombatAI
{
    // Integration with SCAR (Skyrim Combos AI Revolution).
    // SCAR picks attacks from SCAR_ActionData annotations in patched movesets, and patched
    // attack clips raise the SCAR_ComboStart graph event. An actor that raised it recently
    // has a working SCAR moveset, so its normal/power attacks are left to SCAR. Unpatched
    // movesets never raise it and keep our offense; a SCAR actor that stalls in reach gets
    // our attack after a timeout (badly patched movesets).
    class SCARIntegration : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
    {
      public:
        static SCARIntegration &GetInstance()
        {
            static SCARIntegration instance;
            return instance;
        }

        SCARIntegration(const SCARIntegration &) = delete;
        SCARIntegration &operator=(const SCARIntegration &) = delete;

        // Detect SCAR.dll
        void Initialize();

        bool IsAvailable() const { return m_isAvailable; }

        // Call per processed tick for actors in combat: hooks the actor's graph events and
        // records attack activity for the stall check
        void Track(RE::Actor *a_actor, RE::FormID a_formID);

        // Call when our offense wants a normal/power attack. True = leave it to SCAR.
        bool OwnsAttacks(RE::FormID a_formID);

        // True while the actor's moveset is SCAR-driven (no stall bookkeeping)
        bool IsDriving(RE::FormID a_formID) const;

        // Advance the clock (call once per frame)
        void Update(float a_deltaTime);

        void Evict(RE::FormID a_formID) { m_states.Erase(a_formID); }

        RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent *a_event,
                                              RE::BSTEventSource<RE::BSAnimationGraphEvent> *a_source) override;

      private:
        SCARIntegration() = default;
        ~SCARIntegration() override = default;

        // Times are seconds on m_time
        struct ActorSCARState
        {
            float lastComboTime = -1000.0f;  // Last SCAR_ComboStart
            float lastAttackTime = -1000.0f; // Last tick seen attacking
            float wantSince = -1.0f;         // Our offense has wanted to attack since (-1 = not waiting)
            float lastWantTime = -1000.0f;   // Last time our offense wanted to attack
        };

        bool m_isAvailable = false;
        std::atomic<float> m_time{0.0f};
        ThreadSafeMap<RE::FormID, ActorSCARState> m_states;
    };
} // namespace CombatAI
