#pragma once

#include "ActionExecutor.h"
#include "ActorStateObserver.h"
#include "DecisionMatrix.h"
#include "DecisionResult.h"
#include "Humanizer.h"
#include "ParryFeedbackTracker.h"
#include "ThreadSafeMap.h"
#include "pch.h"
#include <atomic>

namespace CombatAI
{
    // Singleton manager for all combat AI logic
    class CombatDirector
    {
      public:
        static CombatDirector &GetInstance()
        {
            static CombatDirector instance;
            return instance;
        }

        CombatDirector(const CombatDirector &) = delete;
        CombatDirector &operator=(const CombatDirector &) = delete;

        // Initialize the director
        void Initialize();

        // (Re)apply runtime-tunable config values (processing interval + humanizer).
        // Safe to call on the game thread; used at init and after runtime config edits.
        void ApplyConfig();

        // Register mod callback listeners (for EldenParry integration)
        void RegisterModCallbacks();

        // Process an actor (called from hook)
        void ProcessActor(RE::Actor *a_actor, float a_deltaTime);

        // Update (called per frame, for cooldowns, etc.)
        void Update(float a_deltaTime);

        // Cleanup (remove invalid actors)
        void Cleanup();

        // Record a hit for target switching (called from the TESHitEvent sink)
        void OnActorHit(RE::Actor *a_victim, RE::Actor *a_attacker);

        // An attack or bash animation event was accepted by the actor's graph: the swing really started
        void NotifySwingStarted(RE::Actor *a_actor) { m_observer.NotifySwingStarted(a_actor); }

        // A melee hit landed: learn the attacker's swing timing (parry / timed block)
        void OnHitLanded(RE::Actor *a_attacker, RE::Actor *a_victim, bool a_power)
        {
            m_observer.OnHitLanded(a_attacker, a_victim, a_power);
        }
        void OnHitBlocked(RE::Actor *a_victim) { m_observer.OnHitBlocked(a_victim); }

      private:
        CombatDirector() = default;
        ~CombatDirector() = default;

        // Check if actor should be processed
        bool ShouldProcessActor(RE::Actor *a_actor, RE::FormID a_formID, float a_deltaTime, float &a_elapsed);
        void MaybeSwitchTarget(RE::Actor *a_actor, RE::FormID a_formID, const ActorStateData &a_state);
        DecisionResult ApplyCombatPacing(RE::Actor *a_actor, RE::FormID a_formID, const ActorStateData &a_state,
                                         DecisionResult a_decision);
        void ExecuteDecision(RE::Actor *a_actor, RE::FormID a_formID, const DecisionResult &a_decision,
                             const ActorStateData &a_state);

        // Evict all per-actor bookkeeping (timers, spawn time, processed flag, observer
        // caches) for a FormID. Called when an actor leaves combat / dies / is torn down
        // so a recycled temporary FormID (0xFF...) starts fresh (spawn warmup re-applies)
        // and the maps do not grow unbounded.
        void EvictActor(RE::FormID a_formID);

        // Components
        ActorStateObserver m_observer;
        DecisionMatrix m_decisionMatrix;
        ActionExecutor m_executor;
        Humanizer m_humanizer;

        // Track processed actors (for cleanup) - use FormID as key for safety
        // Thread-safe to prevent crashes from concurrent access
        ThreadSafeSet<RE::FormID> m_processedActors;

        // Track per-actor processing timers (for throttling) - use FormID as key for safety
        // Thread-safe to prevent crashes from concurrent access
        ThreadSafeMap<RE::FormID, float> m_actorProcessTimers;

        // Seconds each actor has been in combat (evicted when it leaves). Gates the
        // spawn warmup and drives humanizer fatigue.
        ThreadSafeMap<RE::FormID, float> m_actorCombatTimes;

        // Target switching: last hit taken per actor and the earliest time it may roll again
        struct RecentHit
        {
            RE::ActorHandle attacker;
            float time = 0.0f;
        };
        ThreadSafeMap<RE::FormID, RecentHit> m_recentHits;
        // Pacing: who last hit this actor, and until when it may fight back without a slot
        struct Retaliation
        {
            RE::FormID attacker = 0;
            float until = 0.0f;
        };
        ThreadSafeMap<RE::FormID, Retaliation> m_retaliation;
        ThreadSafeMap<RE::FormID, float> m_nextTargetSwitchTime;
        // When each actor entered its current melee kDraw or kBash (stale attack-state watchdog)
        struct AttackStateSince
        {
            RE::ATTACK_STATE_ENUM state = RE::ATTACK_STATE_ENUM::kNone;
            float since = 0.0f;
        };
        ThreadSafeMap<RE::FormID, AttackStateSince> m_drawStart;
        std::atomic<float> m_time{0.0f}; // Seconds since load, advanced in Update()

        // Processing throttle (don't process every frame)
        float m_processTimer = 0.0f;
        float m_processInterval = 0.1f; // Process every 100ms (loaded from config)

        // Minimum time before processing newly spawned actors (safety delay)
        static constexpr float SPAWN_WARMUP_DELAY = 0.5f; // 500ms delay
        // Real windups and bashes last under ~1.5 s; a kDraw or kBash this old has no attack behind it
        static constexpr float STALE_DRAW_TIMEOUT = 3.0f;
        // A waiting NPC within this of its wait ring circles; closer in it falls back first (pacing)
        static constexpr float kWaitRingTolerance = 150.0f;
    };
} // namespace CombatAI
