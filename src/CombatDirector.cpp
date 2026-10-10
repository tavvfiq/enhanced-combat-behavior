#include "CombatDirector.h"
#include "APIManager.h"
#include "ActorUtils.h"
#include "AttackCoordinator.h"
#include "AttackDefenseFeedbackTracker.h"
#include "Config.h"
#include "GuardCounterFeedbackTracker.h"
#include "Logger.h"
#include "ModEventSinks.h"
#include "ParryFeedbackTracker.h"
#include "PrecisionIntegration.h"
#include "TimedBlockFeedbackTracker.h"
#include "TimedBlockIntegration.h"
#include "pch.h"
#include <atomic>
#include <sstream>

namespace CombatAI
{
    namespace
    {
        bool IsMovementAction(ActionType a_action)
        {
            switch (a_action) {
            case ActionType::Retreat:
            case ActionType::Strafe:
            case ActionType::Backoff:
            case ActionType::Advancing:
            case ActionType::ForwardDodge:
            case ActionType::Flanking:
                return true;
            default:
                return false;
            }
        }
    } // namespace

    // Track if mod callback events are being received
    // These flags are accessed by ModEventSinks.cpp
    bool s_receivedParryModEvent = false;
    bool s_receivedTimedBlockModEvent = true; // Set to true since mod events work for timed blocks
    void CombatDirector::Initialize()
    {
        LOG_INFO("CombatDirector initialized");

        // Initialize Precision integration (if enabled in config)
        auto &config = Config::GetInstance();
        if (config.GetModIntegrations().enablePrecisionIntegration) {
            PrecisionIntegration::GetInstance().Initialize();
        }

        // Initialize Timed Block integration (if enabled in config)
        if (config.GetTimedBlock().enableTimedBlock) {
            TimedBlockIntegration::GetInstance().Initialize();
        }

        if (config.GetModIntegrations().enableBFCOIntegration) {
            // Check if BFCO plugin is loaded
            auto dataHandler = RE::TESDataHandler::GetSingleton();
            if (dataHandler) {
                auto bfcoPlugin = dataHandler->LookupModByName("SCSI-ACTbfco-Main.esp");
                if (bfcoPlugin) {
                    m_executor.EnableBFCO(true);
                }
            } else {
                m_executor.EnableBFCO(false);
            }
        }

        // Apply processing interval + humanizer values from config
        ApplyConfig();

        // Register mod callback listeners (for EldenParry integration)
        RegisterModCallbacks();
    }

    void CombatDirector::ApplyConfig()
    {
        auto &config = Config::GetInstance();

        // Set processing interval
        m_processInterval = config.GetGeneral().processingInterval;

        // Push humanizer config values into the humanizer (copied by value)
        Humanizer::Config humanizerConfig;
        humanizerConfig.baseReactionDelayMs = config.GetHumanizer().baseReactionDelayMs;
        humanizerConfig.reactionVarianceMs = config.GetHumanizer().reactionVarianceMs;
        humanizerConfig.bashCooldownSeconds = config.GetHumanizer().bashCooldownSeconds;
        humanizerConfig.dodgeCooldownSeconds = config.GetHumanizer().dodgeCooldownSeconds;
        humanizerConfig.jumpCooldownSeconds = config.GetHumanizer().jumpCooldownSeconds;
        humanizerConfig.bashMistakeMultiplier = config.GetHumanizer().bashMistakeMultiplier;
        humanizerConfig.dodgeMistakeMultiplier = config.GetHumanizer().dodgeMistakeMultiplier;
        humanizerConfig.jumpMistakeMultiplier = config.GetHumanizer().jumpMistakeMultiplier;
        humanizerConfig.strafeMistakeMultiplier = config.GetHumanizer().strafeMistakeMultiplier;
        humanizerConfig.powerAttackMistakeMultiplier = config.GetHumanizer().powerAttackMistakeMultiplier;
        humanizerConfig.attackMistakeMultiplier = config.GetHumanizer().attackMistakeMultiplier;
        humanizerConfig.sprintAttackMistakeMultiplier = config.GetHumanizer().sprintAttackMistakeMultiplier;
        humanizerConfig.retreatMistakeMultiplier = config.GetHumanizer().retreatMistakeMultiplier;
        humanizerConfig.backoffMistakeMultiplier = config.GetHumanizer().backoffMistakeMultiplier;
        humanizerConfig.advancingMistakeMultiplier = config.GetHumanizer().advancingMistakeMultiplier;
        humanizerConfig.flankingMistakeMultiplier = config.GetHumanizer().flankingMistakeMultiplier;
        m_humanizer.SetConfig(humanizerConfig);
    }

    void CombatDirector::RegisterModCallbacks()
    {
        auto modCallbackEventSource = SKSE::GetModCallbackEventSource();
        if (!modCallbackEventSource) {
            LOG_WARN("Mod callback event source not available");
            return;
        }

        auto &config = Config::GetInstance();

        // Register parry callbacks if parry is enabled
        if (config.GetParry().enableParry) {
            static EldenParryEventSink parryEventSink;
            modCallbackEventSource->AddEventSink(&parryEventSink);
            LOG_INFO("Registered mod callback listeners for EldenParry integration");
        } else {
            LOG_INFO("Parry is disabled, skipping EldenParry callback registration");
        }

        // Register timed block callbacks if timed block is enabled
        if (config.GetTimedBlock().enableTimedBlock) {
            static TimedBlockEventSink timedBlockEventSink;
            modCallbackEventSource->AddEventSink(&timedBlockEventSink);
            LOG_INFO("Registered mod callback listeners for Simple Timed Block integration");
        } else {
            LOG_INFO("Timed Block is disabled, skipping Simple Timed Block callback "
                     "registration");
        }

        // Register TESHitEvent sink to detect when NPC attacks successfully hit the
        // player This is always enabled as it's needed for hit/miss tracking
        auto eventSourceHolder = RE::ScriptEventSourceHolder::GetSingleton();
        if (eventSourceHolder) {
            static AttackHitEventSink hitEventSink;
            eventSourceHolder->AddEventSink<RE::TESHitEvent>(&hitEventSink);
            LOG_INFO("Registered TESHitEvent sink for attack hit detection");
        } else {
            LOG_WARN("ScriptEventSourceHolder not available for hit detection");
        }
    }

    void CombatDirector::ProcessActor(RE::Actor *a_actor, float a_deltaTime)
    {
        if (!a_actor) {
            return;
        }

        const auto formIDOpt = ActorUtils::SafeGetFormID(a_actor);
        if (!formIDOpt || formIDOpt.value() == RE::FormID(0)) {
            return;
        }
        const RE::FormID formID = formIDOpt.value();

        // Actor_Update may execute concurrently for different actors.
        static std::atomic_uint32_t processActorCallCount{ 0 };
        const std::uint32_t callCount = processActorCallCount.fetch_add(1, std::memory_order_relaxed) + 1;

        auto &config = Config::GetInstance();
        bool debugEnabled = config.GetGeneral().enableDebugLog;

        // Re-apply humanizer config on the game thread if the runtime menu changed it.
        if (config.ConsumeHumanizerDirty()) {
            ApplyConfig();
        }

        if (!ShouldProcessActor(a_actor, formID, a_deltaTime)) {
            return;
        }

        if (debugEnabled && callCount % 100 == 0) {
            LOG_DEBUG("ProcessActor called for actor FormID: 0x{:08X}", static_cast<std::uint32_t>(formID));
        }

        // Check if actor can react (reaction delay). Bail out before the expensive
        // state gathering and decision evaluation when the actor is still within its
        // reaction latency window - there is nothing we could act on this frame anyway.
        if (!m_humanizer.CanReact(a_actor, formID, a_deltaTime)) {
            return; // can't react yet
        }

        // Gather state and evaluate the decision matrix (the heavy per-actor work)
        ActorStateData state = m_observer.GatherState(a_actor, a_deltaTime);
        DecisionResult decision = m_decisionMatrix.Evaluate(a_actor, state);

        decision = ApplyCombatPacing(a_actor, formID, state, decision);

        if (debugEnabled) {
            LOG_DEBUG("Decision: actor=0x{:08X} action={} priority={:.2f}", static_cast<std::uint32_t>(formID),
                      static_cast<int>(decision.action), decision.priority);
        }

        // Briefly let a successful timed block settle before locomotion can release it.
        if (state.self.isBlocking && IsMovementAction(decision.action) &&
            m_humanizer.IsOnCooldown(formID, ActionType::TimedBlock)) {
            if (debugEnabled) {
                LOG_DEBUG("Holding movement transition for actor 0x{:08X}: timed block is still settling",
                          static_cast<std::uint32_t>(formID));
            }
            return;
        }

        // Check if should make mistake (humanizer)
        if (decision.action != ActionType::None && m_humanizer.ShouldMakeMistake(a_actor, decision.action)) {
            // Make a mistake - don't execute the action
            return;
        }

        // Check cooldowns (only actions with cooldowns will return true)
        if (m_humanizer.IsOnCooldown(formID, decision.action)) {
            if (decision.action == ActionType::ForwardDodge) {
                // Keep ordinary gap-closing active while the shared dodge cooldown runs.
                decision.action = ActionType::Advancing;
            } else {
                return; // On cooldown
            }
        }

        if (decision.action != ActionType::None) {
            ExecuteDecision(a_actor, formID, decision, state);
        }
    }

    DecisionResult CombatDirector::ApplyCombatPacing(RE::Actor *a_actor, RE::FormID a_formID,
                                                      const ActorStateData &a_state, DecisionResult a_decision)
    {
        const auto &pacing = Config::GetInstance().GetCombatPacing();
        if (!pacing.enableCombatPacing || !a_state.target.isValid || a_state.target.targetFormID == 0) {
            return a_decision;
        }

        auto &coordinator = AttackCoordinator::GetSingleton();
        const bool isCommit = a_decision.action == ActionType::Attack || a_decision.action == ActionType::PowerAttack ||
                              a_decision.action == ActionType::SprintAttack;
        const bool paced = isCommit && (!pacing.paceTargetPlayerOnly || a_state.target.isPlayer);
        if (paced) {
            if (!coordinator.TryAcquire(a_state.target.targetFormID, a_formID, pacing.maxSimultaneousAttackers,
                                        pacing.slotWindowMinSeconds, pacing.slotWindowMaxSeconds)) {
                a_decision = m_decisionMatrix.EvaluateHeldBack(a_actor, a_state);
                if (Config::GetInstance().GetGeneral().enableDebugLog) {
                    LOG_DEBUG("Pacing: attack slot full - holding back (action now {})",
                              static_cast<int>(a_decision.action));
                }
            }
        } else {
            coordinator.Release(a_state.target.targetFormID, a_formID);
        }
        return a_decision;
    }

    void CombatDirector::ExecuteDecision(RE::Actor *a_actor, RE::FormID a_formID, const DecisionResult &a_decision,
                                         const ActorStateData &a_state)
    {
        DecisionResult executedDecision = a_decision;
        if (!m_executor.Execute(a_actor, executedDecision, a_state)) {
            if (executedDecision.action != ActionType::ForwardDodge) {
                return;
            }
            // If the dodge cannot start (e.g. runtime state changed after evaluation),
            // fall back to the usual advancing path without spending its cooldown.
            executedDecision.action = ActionType::Advancing;
            if (!m_executor.Execute(a_actor, executedDecision, a_state)) {
                return;
            }
        }

        m_processedActors.Insert(a_formID);
        m_humanizer.MarkActionUsed(a_formID, executedDecision.action);
        m_observer.NotifyActionExecuted(a_actor, executedDecision.action);
        APIManager::GetSingleton()->NotifyDecision(a_actor, executedDecision);
    }

    void CombatDirector::Update(float a_deltaTime)
    {
        // Update per-frame systems (must be called once per frame, not per actor)
        m_humanizer.Update(a_deltaTime);
        m_observer.Update(a_deltaTime);
        AttackCoordinator::GetSingleton().Update(a_deltaTime);

        auto &config = Config::GetInstance();

        // Update parry feedback tracker (only if parry is enabled)
        if (config.GetParry().enableParry) {
            ParryFeedbackTracker::GetInstance().Update(a_deltaTime);
        }

        // Update timed block feedback tracker (only if timed block is enabled)
        if (config.GetTimedBlock().enableTimedBlock) {
            TimedBlockFeedbackTracker::GetInstance().Update(a_deltaTime);
        }

        // Update attack defense feedback tracker (always update)
        AttackDefenseFeedbackTracker::GetInstance().Update(a_deltaTime);

        // Update guard counter feedback tracker (always update)
        GuardCounterFeedbackTracker::GetInstance().Update(a_deltaTime);

        // Clean up spawn time entries:
        // 1. Remove entries for actors that have been processed for a while (after
        // warmup period)
        // 2. This also catches actors that left combat (their spawn times stop
        // incrementing)
        m_actorSpawnTimes.WithWriteLock([&](auto &spawnTimesMap) {
            auto spawnIt = spawnTimesMap.begin();
            while (spawnIt != spawnTimesMap.end()) {
                // Clean up old spawn times (actors that have been processed for a while
                // or left combat)
                if (spawnIt->second > SPAWN_WARMUP_DELAY + 5.0f) {
                    // Actor has been processed for 5+ seconds after warmup, remove spawn
                    // time entry This also cleans up entries for actors that left combat
                    // (their times stop incrementing)
                    spawnIt = spawnTimesMap.erase(spawnIt);
                } else {
                    ++spawnIt;
                }
            }
        });

        // Periodic cleanup (from config)
        static float cleanupTimer = 0.0f;
        float cleanupInterval = config.GetPerformance().cleanupInterval;
        cleanupTimer += a_deltaTime;
        if (cleanupTimer > cleanupInterval) {
            Cleanup();
            cleanupTimer = 0.0f;
        }
    }

    void CombatDirector::Cleanup()
    {
        // With FormID keys, cleanup is much simpler
        // FormIDs are stable identifiers - they don't become invalid
        // We rely on lazy cleanup: entries are removed when actors leave combat
        // (checked in ProcessActor when actor is not in combat)
        // This avoids expensive LookupByID() calls that could crash

        // Note: We don't need to do aggressive cleanup here since:
        // 1. FormIDs don't become invalid (unlike pointers)
        // 2. Entries are cleaned up lazily when actors leave combat
        // 3. Avoiding LookupByID() prevents crashes from invalid FormIDs or deleted
        // forms

        // Clean up Humanizer state (it also uses lazy cleanup)
        m_humanizer.Cleanup();

        // Drop empty attack-slot buckets (slots themselves expire by their window)
        AttackCoordinator::GetSingleton().Cleanup();

        // Clean up spawn times for actors no longer in combat
        // This is done lazily in Update(), but we can also clean up here if needed
        // (Spawn times are cleaned up automatically in Update() after warmup period)

        // If we want to be more aggressive, we could add a size limit and remove
        // oldest entries But for now, lazy cleanup is safer and more efficient
    }

    void CombatDirector::EvictActor(RE::FormID a_formID)
    {
        if (a_formID == 0) {
            return;
        }
        m_actorProcessTimers.Erase(a_formID);
        m_actorSpawnTimes.Erase(a_formID);
        m_processedActors.Erase(a_formID);
        m_observer.EvictActor(a_formID);
    }

    bool CombatDirector::ShouldProcessActor(RE::Actor *a_actor, RE::FormID a_formID, float a_deltaTime)
    {
        if (!a_actor || a_formID == RE::FormID(0)) {
            return false;
        }

        // Validate actor using safe wrappers - protects against transitional states
        // Actor is passed directly from hook, but could become invalid at any time

        // Quick validation - if actor is dead or not in combat, skip early. Evict any
        // per-actor state so a recycled temporary FormID (0xFF...) starts fresh and the
        // bookkeeping maps stay bounded.
        bool isDead = ActorUtils::SafeIsDead(a_actor);
        bool inCombat = ActorUtils::SafeIsInCombat(a_actor);
        if (isDead || !inCombat) {
            EvictActor(a_formID);
            return false;
        }

        // Liveness guard: an actor can be in combat yet mid-teardown (3D unloaded,
        // deleted, or disabled). Processing it walks transient/freed game state in the
        // heavier gather path. Skip and evict so recycled FormIDs don't inherit state.
        if (!ActorUtils::SafeIs3DLoaded(a_actor) || ActorUtils::SafeIsDeleted(a_actor) ||
            ActorUtils::SafeIsDisabled(a_actor)) {
            EvictActor(a_formID);
            return false;
        }

        // Skip player
        if (ActorUtils::SafeIsPlayerRef(a_actor)) {
            return false;
        }

        // Only process NPCs, not creatures
        // Check for ActorTypeNPC keyword (NPCs have this, creatures don't)
        bool hasNPCCheck = ActorUtils::SafeHasKeywordString(a_actor, "ActorTypeNPC");
        if (!hasNPCCheck) {
            return false;
        }

        // Double-check: explicitly exclude creatures
        if (ActorUtils::SafeHasKeywordString(a_actor, "ActorTypeCreature")) {
            return false;
        }

        // Additional check: use CalculateCachedOwnerIsNPC as fallback
        // This should match the keyword check, but provides extra safety
        if (!ActorUtils::SafeCalculateCachedOwnerIsNPC(a_actor)) {
            return false;
        }

        // Check if we should only process combat actors
        auto &config = Config::GetInstance();
        if (config.GetPerformance().onlyProcessCombatActors) {
            // Only process actors in combat (already checked above, but check again for
            // consistency)
            if (!ActorUtils::SafeIsInCombat(a_actor)) {
                return false;
            }
        }

        // Check if AI is enabled
        if (!ActorUtils::SafeIsAIEnabled(a_actor)) {
            return false;
        }

        // Check spawn warmup delay for newly spawned actors
        // This prevents processing actors before they're fully initialized
        auto spawnTimeOpt = m_actorSpawnTimes.Find(a_formID);
        if (!spawnTimeOpt.has_value()) {
            // First time seeing this actor - record spawn time
            // Start at 0.0f, we'll increment it each frame
            m_actorSpawnTimes.Emplace(a_formID, 0.0f);
            // Don't process newly spawned actors immediately
            return false;
        }

        // Increment spawn time using the deltaTime for this actor
        // This ensures spawn times are updated even if Update() hasn't been called
        // yet. The read-modify-write is done atomically under the map's lock.
        float currentSpawnTime = 0.0f;
        m_actorSpawnTimes.Modify(a_formID, [&](float &spawnTime) {
            spawnTime += a_deltaTime;
            currentSpawnTime = spawnTime;
        });

        // Check if actor is still in warmup period
        if (currentSpawnTime < SPAWN_WARMUP_DELAY) {
            // Actor is still warming up, don't process yet
            return false;
        }

        // Determine processing interval based on distance to player (LOD)
        // Get player position - safely
        auto player = RE::PlayerCharacter::GetSingleton();
        float targetInterval = m_processInterval; // Default to near interval

        if (player) {
            auto playerPos = player->GetPosition();
            auto actorPosOpt = ActorUtils::SafeGetPosition(a_actor);

            if (actorPosOpt.has_value()) {
                float distSq = playerPos.GetSquaredDistance(actorPosOpt.value());
                float nearDistSq = config.GetPerformance().distanceNear * config.GetPerformance().distanceNear;
                float midDistSq = config.GetPerformance().distanceMid * config.GetPerformance().distanceMid;

                if (distSq > midDistSq) {
                    // Far distance - slowest updates
                    targetInterval = config.GetPerformance().processingIntervalFar;
                } else if (distSq > nearDistSq) {
                    // Mid distance - medium updates
                    targetInterval = config.GetPerformance().processingIntervalMid;
                }
            }
        }

        // Update the per-actor throttle timer atomically under the map's lock and
        // decide whether enough time has elapsed to process this actor this frame.
        bool shouldProcess = false;
        bool existed = m_actorProcessTimers.Modify(a_formID, [&](float &timer) {
            timer += a_deltaTime;
            if (timer >= targetInterval) {
                timer = 0.0f; // Reset timer for next interval
                shouldProcess = true;
            }
        });

        if (!existed) {
            // First time processing this actor, initialize timer and allow processing
            m_actorProcessTimers.Emplace(a_formID, 0.0f);
            return true;
        }

        return shouldProcess;
    }
} // namespace CombatAI
