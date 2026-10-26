#include "CombatDirector.h"
#include "APIManager.h"
#include "ActorUtils.h"
#include "AttackCoordinator.h"
#include "AttackDefenseFeedbackTracker.h"
#include "Config.h"
#include "EngineProbes.h"
#include "ProjectileAwareness.h"
#include "GuardCounterFeedbackTracker.h"
#include "Intents.h"
#include "SCARData.h"
#include "Boss.h"
#include "Personality.h"
#include "ComboFollowUp.h"
#include "Trees.h"
#include "Logger.h"
#include "ModEventSinks.h"
#include "ParryFeedbackTracker.h"
#include "PrecisionIntegration.h"
#include "SCARIntegration.h"
#include "TimedBlockFeedbackTracker.h"
#include "TimedBlockIntegration.h"
#include "VanillaAttackShaper.h"
#include "pch.h"
#include <atomic>
#include <random>
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

    void CombatDirector::Initialize()
    {
        LOG_INFO("CombatDirector initialized");
        DecisionMatrix::RollSelfTest();

        // Initialize Precision integration (if enabled in config)
        auto &config = Config::GetInstance();
        if (config.GetModIntegrations().enablePrecisionIntegration) {
            PrecisionIntegration::GetInstance().Initialize();
        }

        // Initialize Timed Block integration (if enabled in config)
        if (config.GetTimedBlock().enableTimedBlock) {
            TimedBlockIntegration::GetInstance().Initialize();
        }

        SCARIntegration::GetInstance().Initialize();


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
        m_humanizer.SetConfig(config.GetHumanizer());
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

        auto &config = Config::GetInstance();
        const bool debugEnabled = config.IsDebugTarget(formID);

        // Re-apply humanizer config on the game thread if the runtime menu changed it.
        if (config.ConsumeHumanizerDirty()) {
            ApplyConfig();
        }

        // Time since this actor was last processed: the temporal timers advance by it, not by one frame
        float elapsed = a_deltaTime;
        if (!ShouldProcessActor(a_actor, formID, a_deltaTime, elapsed)) {
            return;
        }
        // A direct block whose tree thread never ran its Exit (tree torn down): release it here
        if (Intents::DualBlockOverdue(a_actor)) {
            Trees::ReleaseDualBlock(a_actor, true);
        }

        SCARIntegration::GetInstance().Track(a_actor, formID);

        // Check if actor can react (reaction delay). Bail out before the expensive
        // state gathering and decision evaluation when the actor is still within its
        // reaction latency window - there is nothing we could act on this frame anyway.
        if (!m_humanizer.CanReact(a_actor, formID)) {
            if (debugEnabled) {
                LOG_DEBUG("Skip 0x{:08X}: combat-entry reaction delay", static_cast<std::uint32_t>(formID));
            }
            return; // can't react yet
        }

        if (debugEnabled) {
            // What CPR is actually told, and whether the NPC is sprinting, each tick
            auto gb = [&](const char *a_var) {
                bool v = false;
                return a_actor->GetGraphVariableBool(a_var, v) && v;
            };
            auto gf = [&](const char *a_var) {
                float v = -1.0f;
                a_actor->GetGraphVariableFloat(a_var, v);
                return v;
            };
            LOG_DEBUG("CPR 0x{:08X}: advance={} inner=({:.0f},{:.0f},{:.0f}) outer=({:.0f},{:.0f},{:.0f}) circling={} "
                      "fallback={} backoff={} | sprinting={} Speed={:.0f}",
                      static_cast<std::uint32_t>(formID), gb("CPR_EnableAdvanceRadius"), gf("CPR_InnerRadiusMin"),
                      gf("CPR_InnerRadiusMid"), gf("CPR_InnerRadiusMax"), gf("CPR_OuterRadiusMin"),
                      gf("CPR_OuterRadiusMid"), gf("CPR_OuterRadiusMax"), gb("CPR_EnableCircling"),
                      gb("CPR_EnableFallback"), gb("CPR_EnableBackoff"), ActorUtils::SafeIsSprinting(a_actor),
                      gf("Speed"));
        }

        // Gather state and evaluate the decision matrix (the heavy per-actor work)
        ActorStateData state = m_observer.GatherState(a_actor, elapsed);
        Boss::Get(a_actor); // detection only for now (logs each boss once)
        // A held dual block ends when the swing stops without hitting; a landed blow ends it from the hit event
        // (ModEventSinks). Not on the attacker's recovery state: between combo swings it is set before the next blow,
        // and run 40 dropped 30 blocks ~0.15 s in that way (5 of 73 hits blocked).
        if (Intents::WantsDualBlock(a_actor) && !state.target.isAttacking) {
            Intents::EndDualBlockHold(a_actor);
            if (debugEnabled) {
                LOG_DEBUG("DualBlock 0x{:08X}: target stopped attacking, ending the hold", static_cast<std::uint32_t>(formID));
            }
        }
        MaybeSwitchTarget(a_actor, formID, state);
        DecisionResult decision = m_decisionMatrix.Evaluate(a_actor, state);
        // Debug: which SCAR actions this NPC's active clip offers and which would fit now (read-only)
        if (debugEnabled && state.target.isValid &&
            (decision.action == ActionType::Attack || decision.action == ActionType::PowerAttack ||
             state.self.attackState == RE::ATTACK_STATE_ENUM::kNextAttack)) {
            if (auto target = RE::TESForm::LookupByID<RE::Actor>(state.target.targetFormID)) {
                SCARData::Probe(a_actor, target, state.baseWeaponReach);
            }
        }
        // Shot factor for archers; for everyone else a cast hold while an ally is in the line of fire
        if (config.GetDecisionMatrix().enableRangedPositioning && state.target.isValid) {
            const float shot = state.self.isRanged ? DecisionMatrix::RangedShotFactor(state)
                                                   : (DecisionMatrix::AllyInLineOfFire(state) ? 0.0f : 1.0f);
            const float posted = Intents::PostRangedShot(a_actor, shot);
            if (debugEnabled && (shot != 1.0f || posted != 1.0f)) {
                LOG_DEBUG("Ranged 0x{:08X}: shot chance x{:.1f}{}", static_cast<std::uint32_t>(formID), posted,
                          posted != shot ? " (held 3 s, shooting anyway)" : "");
            }
        }

        decision = ApplyCombatPacing(a_actor, formID, state, decision);

        // A dodge, bash or sprint change that cuts an attack's windup can leave the engine
        // in kDraw with no attack playing. Vanilla AI then never starts another swing until
        // a hit reaction resets it, so clear a kDraw that outlives any real windup.
        if (state.self.attackState == RE::ATTACK_STATE_ENUM::kDraw && state.self.isMelee) {
            const float now = m_time.load(std::memory_order_relaxed);
            m_drawStart.Emplace(formID, now);
            const float drawSince = m_drawStart.Find(formID).value_or(now);
            if (now - drawSince > STALE_DRAW_TIMEOUT) {
                if (auto *actorState = ActorUtils::SafeAsActorState(a_actor)) {
                    actorState->actorState1.meleeAttackState = RE::ATTACK_STATE_ENUM::kNone;
                    state.self.attackState = RE::ATTACK_STATE_ENUM::kNone;
                    LOG_DEBUG("Cleared stale kDraw on 0x{:08X} after {:.1f}s", static_cast<std::uint32_t>(formID),
                              now - drawSince);
                }
                m_drawStart.Erase(formID);
            }
        } else {
            m_drawStart.Erase(formID);
        }

        // Never pull an NPC back mid-swing: its attack (often SCAR's, which bypasses our
        // hooks) would whiff at range. After the hit (next-attack window, follow-through) it may back off.
        switch (state.self.attackState) {
        case RE::ATTACK_STATE_ENUM::kDraw:
        case RE::ATTACK_STATE_ENUM::kSwing:
        case RE::ATTACK_STATE_ENUM::kHit:
        case RE::ATTACK_STATE_ENUM::kBash:
            m_executor.SuspendRetreat(a_actor);
            if (decision.action == ActionType::Retreat || decision.action == ActionType::Backoff) {
                if (debugEnabled) {
                    LOG_DEBUG("Skip 0x{:08X}: no {} mid-swing", static_cast<std::uint32_t>(formID),
                              ActionName(decision.action));
                }
                return;
            }
            break;
        default:
            break;
        }

        if (debugEnabled) {
            LOG_DEBUG("Decision: actor=0x{:08X} action={} priority={:.2f}", static_cast<std::uint32_t>(formID),
                      ActionName(decision.action), decision.priority);
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
        const float combatTime = m_actorCombatTimes.Find(formID).value_or(0.0f);
        // Skill roll, once per opportunity (a projectile sidestep has its own roll)
        // The next-best option the brain had this tick, never the excluded action, never a pull-back mid-swing
        const auto nextBest = [&](ActionType a_excluded) {
            const bool midSwing = state.self.attackState != RE::ATTACK_STATE_ENUM::kNone &&
                                  state.self.attackState != RE::ATTACK_STATE_ENUM::kNextAttack &&
                                  state.self.attackState != RE::ATTACK_STATE_ENUM::kFollowThrough;
            DecisionResult best;
            for (const auto &option : DecisionMatrix::LastCandidates()) {
                const bool pullsBack = option.action == ActionType::Retreat || option.action == ActionType::Backoff;
                if (option.action != a_excluded && option.action != ActionType::None && !(midSwing && pullsBack) &&
                    (best.action == ActionType::None || option.priority > best.priority)) {
                    best = option;
                }
            }
            return best.action != ActionType::None ? ApplyCombatPacing(a_actor, formID, state, best) : best;
        };

        // Backing off is a choice, not a reflex: one roll per swing (the target's, or its own just after the hit)
        // decides whether it steps back this time. Run 18: in range it backed off 213 times against 81 attacks.
        if (decision.action == ActionType::Backoff && !decision.paced) {
            const auto &matrix = Config::GetInstance().GetDecisionMatrix();
            const bool ownSwing = state.self.attackState == RE::ATTACK_STATE_ENUM::kNextAttack ||
                                  state.self.attackState == RE::ATTACK_STATE_ENUM::kFollowThrough;
            const bool targetSwing = state.target.isAttacking && !state.target.isPowerAttacking &&
                                     state.temporal.target.attackStartTime >= 0.0f;
            // Winning the damage race halves both chances: it keeps pressing
            // Personality: careful NPCs back off more (x0.6 .. x1.4)
            const float pressing = (DecisionMatrix::Race(state).winning ? 0.5f : 1.0f) *
                                   Personality::CautionScale(Personality::Get(a_actor).caution);
            std::optional<bool> rolled;
            if (ownSwing) {
                const float swingStart = m_humanizer.Now() - state.temporal.self.timeSinceLastAttack; // constant over one swing
                rolled = m_humanizer.RollBackoff(formID, (1ull << 62) | static_cast<std::uint32_t>(swingStart * 4.0f),
                                                 matrix.backoffAfterSwingChance * pressing);
            } else if (targetSwing) {
                rolled = m_humanizer.RollBackoff(formID,
                                                 (static_cast<std::uint64_t>(state.target.targetFormID) << 32) |
                                                     state.temporal.target.swingCount,
                                                 matrix.backoffOnSwingChance * pressing);
            }
            if (rolled && !*rolled) {
                // Not a sidestep either: run 19 swapped 177 of 273 failed rolls for Strafe, so it still disengaged
                DecisionResult instead = nextBest(ActionType::Backoff);
                if (IsMovementAction(instead.action)) {
                    instead = DecisionResult();
                }
                if (debugEnabled) {
                    LOG_DEBUG("Skip 0x{:08X}: no back off this swing ({}) - {} instead", static_cast<std::uint32_t>(formID),
                              ownSwing ? "own swing" : "target's swing", ActionName(instead.action));
                }
                if (instead.action == ActionType::None) {
                    ComboFollowUp::Update(a_actor, state, instead);
                    return; // vanilla keeps the NPC: it blocks, swings again or holds
                }
                decision = instead;
            }
        }
        // MCO/BFCO: the next attack inside an open combo window, after the back-off roll (run 51: 84 windows were
        // skipped as "defending" on a Backoff that roll then dropped)
        ComboFollowUp::Update(a_actor, state, decision);

        // A failed roll is a mistake, not a freeze: the NPC takes the next-best option it had this tick.
        if (decision.action != ActionType::None && !decision.reflex &&
            !m_humanizer.PassesSkillCheck(a_actor, formID, decision.action, state, combatTime)) {
            const ActionType failed = decision.action;
            const DecisionResult mistake = nextBest(failed);
            if (debugEnabled) {
                LOG_DEBUG("Skip 0x{:08X}: failed skill roll on {} (skill {:.0f}%, combat time {:.0f}s) - {} instead",
                          static_cast<std::uint32_t>(formID), ActionName(failed),
                          m_humanizer.SkillChance(a_actor, failed, combatTime, state.self) * 100.0f, combatTime,
                          ActionName(mistake.action));
            }
            if (mistake.action == ActionType::None) {
                return;
            }
            decision = mistake;
        }

        // Check cooldowns (only actions with cooldowns will return true)
        if (m_humanizer.IsOnCooldown(formID, decision.action)) {
            if (decision.action == ActionType::ForwardDodge || decision.action == ActionType::SprintAttack) {
                // Keep ordinary gap-closing active while the cooldown runs.
                decision.action = ActionType::Advancing;
            } else {
                if (debugEnabled) {
                    LOG_DEBUG("Skip 0x{:08X}: {} on cooldown", static_cast<std::uint32_t>(formID),
                              ActionName(decision.action));
                }
                return; // On cooldown
            }
        }

        if (decision.action != ActionType::None) {
            ExecuteDecision(a_actor, formID, decision, state);
        } else {
            m_executor.ReleaseSprint(a_actor); // don't leave a forced sprint running in place
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
        const RE::FormID target = a_state.target.targetFormID;
        if (pacing.paceTargetPlayerOnly && !a_state.target.isPlayer) {
            coordinator.Release(target, a_formID);
            coordinator.ClearHeld(a_formID);
            return a_decision;
        }

        // Slots follow who is actually engaging - attacking from any source (ours, vanilla,
        // SCAR) or close enough to swing - not just our own attack decisions, which SCAR
        // movesets and the vanilla AI bypass. (Wait Your Turn manages roles the same way.)
        const auto &matrix = Config::GetInstance().GetDecisionMatrix();
        const float reach = StateHelpers::EffectiveAttackRange(a_state.weaponReach, a_state.target.boundRadius,
                                                               matrix.offenseReachMultiplier,
                                                               matrix.offenseReachSafetyMargin);
        const bool attacking = a_state.self.attackState != RE::ATTACK_STATE_ENUM::kNone;
        const bool commit = a_decision.action == ActionType::Attack || a_decision.action == ActionType::PowerAttack ||
                            a_decision.action == ActionType::SprintAttack || a_decision.action == ActionType::Bash;
        // Archers and mages at range never take a slot: a bow draw counts as attacking (run 49: an archer at 1400+
        // was held back and granted a slot 11 times). Pacing is about crowding the target inside melee reach.
        const bool engaging = a_state.target.distance <= reach + pacing.engageMargin;
        if (!engaging && (DecisionMatrix::IsRangedFighter(a_state) || (!attacking && !commit))) {
            coordinator.Release(target, a_formID);
            coordinator.ClearHeld(a_formID);
            return a_decision;
        }

        const bool debug = Config::GetInstance().IsDebugTarget(a_formID);
        const bool wasHeld = coordinator.IsHeld(a_formID);
        const auto hit = m_retaliation.Find(a_formID);
        const bool retaliating =
            hit && hit->attacker == target && m_time.load(std::memory_order_relaxed) < hit->until;
        if (retaliating || coordinator.TryAcquire(target, a_formID, pacing.maxSimultaneousAttackers,
                                                  pacing.slotWindowMinSeconds, pacing.slotWindowMaxSeconds)) {
            coordinator.ClearHeld(a_formID);
            if (debug && wasHeld) {
                LOG_DEBUG("Pacing 0x{:08X}: {} - may engage", static_cast<std::uint32_t>(a_formID),
                          retaliating ? "retaliating" : "slot granted");
            }
            return a_decision;
        }

        // Waiting its turn. The vanilla attack hook vetoes its swings meanwhile.
        coordinator.MarkHeld(a_formID);
        switch (a_decision.action) {
        case ActionType::Dodge:
        case ActionType::Parry:
        case ActionType::TimedBlock:
        case ActionType::Retreat:
        case ActionType::Backoff:
        case ActionType::Jump:
            return a_decision; // defending is always allowed
        default:
            break;
        }
        if (attacking) {
            return a_decision; // never cancel a swing in progress
        }

        // Wait outside reach: circle on a ring, or step back first if already inside reach
        const float holdDistance = reach + pacing.waitDistanceMargin;
        DecisionResult held;
        if (a_state.target.distance <= reach) {
            held.action = ActionType::Backoff;
            held.priority = 1.0f;
            held.paced = true;
        } else {
            held = m_decisionMatrix.EvaluateHeldBack(a_actor, a_state);
            held.holdDistance = holdDistance;
        }
        if (debug && !wasHeld) {
            LOG_DEBUG("Pacing 0x{:08X}: slots full - waiting its turn ({} at {:.0f}, dist {:.0f})",
                      static_cast<std::uint32_t>(a_formID), ActionName(held.action), holdDistance,
                      a_state.target.distance);
        }
        return held;
    }

    void CombatDirector::OnActorHit(RE::Actor *a_victim, RE::Actor *a_attacker)
    {
        if (!a_victim || !a_attacker || a_victim == a_attacker || ActorUtils::SafeIsPlayerRef(a_victim)) {
            return;
        }

        // Pacing: a waiting NPC that gets hit may fight back for a moment (no free hits)
        const auto &pacing = Config::GetInstance().GetCombatPacing();
        if (pacing.enableCombatPacing) {
            thread_local std::mt19937 gen{std::random_device{}()};
            const float until = m_time.load(std::memory_order_relaxed) +
                                std::uniform_real_distribution<float>(pacing.retaliateMinSeconds,
                                                                      pacing.retaliateMaxSeconds)(gen);
            const Retaliation entry{a_attacker->GetFormID(), until};
            m_retaliation.ModifyOrCreate(a_victim->GetFormID(), [&](Retaliation &r) { r = entry; });
        }

        if (!Config::GetInstance().GetTargetSwitching().enableTargetSwitching) {
            return;
        }
        const RecentHit hit{a_attacker->GetHandle(), m_time.load(std::memory_order_relaxed)};
        m_recentHits.ModifyOrCreate(a_victim->GetFormID(), [&](RecentHit &entry) { entry = hit; });
    }

    void CombatDirector::MaybeSwitchTarget(RE::Actor *a_actor, RE::FormID a_formID, const ActorStateData &a_state)
    {
        const auto &cfg = Config::GetInstance().GetTargetSwitching();
        if (!cfg.enableTargetSwitching || !a_state.target.isValid) {
            return;
        }
        const float now = m_time.load(std::memory_order_relaxed);
        if (now < m_nextTargetSwitchTime.Find(a_formID).value_or(0.0f)) {
            return;
        }

        RE::ActorHandle candidate;
        float chance = 0.0f;
        const char *reason = "";

        // Retaliate: turn on whoever just hit us, if they're close enough to fight back
        if (auto hit = m_recentHits.Find(a_formID)) {
            m_recentHits.Erase(a_formID); // one roll per hit
            const auto attacker = hit->attacker.get();
            if (attacker && now - hit->time <= cfg.retaliateWindowSeconds &&
                attacker->GetFormID() != a_state.target.targetFormID && !ActorUtils::SafeIsDead(attacker.get()) &&
                ActorUtils::SafeIsHostileToActor(a_actor, attacker.get()) &&
                a_state.self.position.GetDistance(attacker->GetPosition()) <= cfg.retaliateMaxDistance) {
                candidate = hit->attacker;
                chance = cfg.retaliateChance;
                reason = "retaliate";
            }
        }

        // Finisher: a nearly dead enemy close by beats a healthy current target
        const auto &ctx = a_state.combatContext;
        if (!candidate && ctx.weakestEnemy && ctx.weakestEnemyHealth <= cfg.finisherHealthThreshold &&
            ctx.weakestEnemyDistance <= cfg.finisherMaxDistance &&
            a_state.target.healthPercent > ctx.weakestEnemyHealth + 0.25f) {
            candidate = ctx.weakestEnemy;
            chance = cfg.finisherChance;
            reason = "finisher";
        }

        if (!candidate) {
            return;
        }
        m_nextTargetSwitchTime.ModifyOrCreate(a_formID, [&](float &next) { next = now + cfg.switchCooldownSeconds; });

        thread_local std::mt19937 gen{std::random_device{}()};
        if (std::uniform_real_distribution<float>(0.0f, 1.0f)(gen) >= chance) {
            return;
        }

        if (Config::GetInstance().IsDebugTarget(a_formID)) {
            LOG_DEBUG("TargetSwitch 0x{:08X}: {}", static_cast<std::uint32_t>(a_formID), reason);
        }

        // StartCombat touches combat groups; run it on the main thread and re-validate there.
        if (auto *tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([self = a_actor->GetHandle(), candidate]() {
                const auto actor = self.get();
                const auto target = candidate.get();
                if (actor && target && !actor->IsDead() && !target->IsDead() && actor->IsInCombat() &&
                    target->IsInCombat()) {
                    actor->StartCombat(target.get());
                }
            });
        }
    }

    void CombatDirector::ExecuteDecision(RE::Actor *a_actor, RE::FormID a_formID, const DecisionResult &a_decision,
                                         const ActorStateData &a_state)
    {
        DecisionResult executedDecision = a_decision;
        // Power moves need stamina (one can empty the bar); without it the NPC swings light or keeps closing
        const float powerMin = Config::GetInstance().GetDecisionMatrix().powerAttackMinStamina;
        if (a_state.self.staminaPercent < powerMin) {
            if (executedDecision.action == ActionType::PowerAttack) {
                executedDecision.action = ActionType::Attack;
            } else if (executedDecision.action == ActionType::SprintAttack) {
                executedDecision.action = ActionType::Advancing;
            }
        }
        // A sprint attack starts with a run-up: sprint at the target until actually
        // sprinting and inside launch range, then launch.
        if (executedDecision.action == ActionType::SprintAttack && !m_executor.CanLaunchSprintAttack(a_actor, a_state)) {
            executedDecision.action = ActionType::Advancing;
            executedDecision.sprint = true;
            executedDecision.intensity = 1.0f;
            executedDecision.direction = a_state.target.position - a_state.self.position;
            executedDecision.direction.z = 0.0f;
            executedDecision.direction.Unitize();
        }
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

        if (Config::GetInstance().IsDebugTarget(a_formID)) {
            LOG_DEBUG("Executed 0x{:08X}: {}{} (priority {:.2f}) target 0x{:08X} dist {:.0f} hp {:.0f}%/{:.0f}%",
                      static_cast<std::uint32_t>(a_formID), ActionName(executedDecision.action),
                      executedDecision.sprint ? " (sprint-attack run-up)" : "",
                      executedDecision.priority, static_cast<std::uint32_t>(a_state.target.targetFormID),
                      a_state.target.distance, a_state.self.healthPercent * 100.0f,
                      a_state.target.healthPercent * 100.0f);
        }

        switch (executedDecision.action) {
        case ActionType::Dodge:
        case ActionType::Parry:
        case ActionType::TimedBlock:
        case ActionType::Retreat:
        case ActionType::Backoff:
        case ActionType::Jump:
            Intents::PostAttackVeto(a_actor, 0.4f); // SCAR attacks bypass the tree; the SCAR gate reads this
            break;
        default:
            break;
        }

        m_processedActors.Insert(a_formID);
        m_humanizer.MarkActionUsed(a_formID, executedDecision.action);
        m_observer.NotifyActionExecuted(a_actor, executedDecision.action);
        APIManager::GetSingleton()->NotifyDecision(a_actor, executedDecision);
    }

    void CombatDirector::Update(float a_deltaTime)
    {
        m_time.store(m_time.load(std::memory_order_relaxed) + a_deltaTime, std::memory_order_relaxed);

        // Update per-frame systems (must be called once per frame, not per actor)
        m_humanizer.Update(a_deltaTime);
        Intents::LogSummary(a_deltaTime);
        SCARIntegration::GetInstance().Update(a_deltaTime);
        m_executor.UpdateJumps(a_deltaTime);
        m_observer.Update(a_deltaTime);
        AttackCoordinator::GetSingleton().Update(a_deltaTime);
        EngineProbes::Update(a_deltaTime);
        ProjectileAwareness::GetInstance().Update();

        auto &config = Config::GetInstance();

        if (config.GetGeneral().enableDebugLog) {
            static RE::FormID followed = 0;
            const auto selected = RE::Console::GetSelectedRef();
            const RE::FormID current = selected ? selected->GetFormID() : 0;
            if (current != followed) {
                followed = current;
                if (current) {
                    LOG_INFO("Debug log now follows console selection 0x{:08X} ({})",
                             static_cast<std::uint32_t>(current), selected->GetDisplayFullName());
                    if (auto *actor = selected->As<RE::Actor>()) {
                        const auto boss = Boss::Get(actor);
                        LOG_INFO("Boss check 0x{:08X}: {} (boss marker {}, listed name {}, EditorID {}, level {})",
                                 static_cast<std::uint32_t>(current), boss.boss ? "BOSS" : "not a boss", boss.marker,
                                 boss.named, boss.editorID ? boss.match : std::string("no"), actor->GetLevel());
                    }
                } else {
                    LOG_INFO("Debug log: no console selection - per-actor logs are off");
                }
            }
        }

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

        // Combat times are evicted in EvictActor() when an actor dies or leaves combat.
        // Do NOT expire them by age: a missing entry re-triggers the spawn warmup.

        // Drop hits nobody rolled on (victim never processed, e.g. a creature)
        const float now = m_time.load(std::memory_order_relaxed);
        const float hitWindow = Config::GetInstance().GetTargetSwitching().retaliateWindowSeconds;
        m_recentHits.WithWriteLock([&](auto &hits) {
            std::erase_if(hits, [&](const auto &entry) { return now - entry.second.time > hitWindow; });
        });

        // If we want to be more aggressive, we could add a size limit and remove
        // oldest entries But for now, lazy cleanup is safer and more efficient
    }

    void CombatDirector::EvictActor(RE::FormID a_formID)
    {
        if (a_formID == 0) {
            return;
        }
        m_actorProcessTimers.Erase(a_formID);
        m_actorCombatTimes.Erase(a_formID);
        m_recentHits.Erase(a_formID);
        m_retaliation.Erase(a_formID);
        m_humanizer.Evict(a_formID);
        AttackCoordinator::GetSingleton().ClearHeld(a_formID);
        m_nextTargetSwitchTime.Erase(a_formID);
        m_drawStart.Erase(a_formID);
        m_processedActors.Erase(a_formID);
        SCARIntegration::GetInstance().Evict(a_formID);
        m_executor.EvictActor(a_formID);
        ProjectileAwareness::GetInstance().Evict(a_formID);
        if (Intents::DualBlockStarted(a_formID)) { // left combat or died mid-block
            Trees::ReleaseDualBlock(RE::TESForm::LookupByID<RE::Actor>(a_formID), true);
        }
        Intents::Evict(a_formID);
        m_observer.EvictActor(a_formID);
    }

    bool CombatDirector::ShouldProcessActor(RE::Actor *a_actor, RE::FormID a_formID, float a_deltaTime, float &a_elapsed)
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

        auto &config = Config::GetInstance();

        // Check if AI is enabled
        if (!ActorUtils::SafeIsAIEnabled(a_actor)) {
            return false;
        }

        // Check spawn warmup delay for newly spawned actors
        // This prevents processing actors before they're fully initialized
        auto spawnTimeOpt = m_actorCombatTimes.Find(a_formID);
        if (!spawnTimeOpt.has_value()) {
            // First time seeing this actor - record spawn time
            // Start at 0.0f, we'll increment it each frame
            m_actorCombatTimes.Emplace(a_formID, 0.0f);
            // Don't process newly spawned actors immediately
            return false;
        }

        // Advance combat time (warmup gate + humanizer fatigue). The read-modify-write
        // is done atomically under the map's lock.
        float currentSpawnTime = 0.0f;
        m_actorCombatTimes.Modify(a_formID, [&](float &combatTime) {
            combatTime += a_deltaTime;
            currentSpawnTime = combatTime;
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
                a_elapsed = timer;
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
