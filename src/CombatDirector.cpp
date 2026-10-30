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
#include "ClipTiming.h"
#include "Personality.h"
#include "Potions.h"
#include "SelfShouts.h"
#include "DefenseTiming.h"
#include "TargetDanger.h"
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

        // Gather state and evaluate the decision matrix (the heavy per-actor work)
        ActorStateData state = m_observer.GatherState(a_actor, elapsed);
        Boss::UpdatePhase(a_actor, state.self.healthPercent); // detection (logged once) and health phase
        TargetDanger::Update(a_actor, state, debugEnabled);   // before the decision: caution and threat read it
        if (state.self.staminaPercent > 0.0f) {                // exertion fatigue: stamina spent lately
            m_humanizer.TrackExertion(formID, state.self.currentStamina,
                                      state.self.currentStamina / state.self.staminaPercent, elapsed);
        }
        Potions::Update(a_actor, state, debugEnabled);         // before the decision: a drink wait makes it retreat
        SelfShouts::Update(a_actor, state, debugEnabled);
        // A held dual block ends when the swing stops without hitting; a landed blow ends it from the hit event
        // (ModEventSinks). Not on the attacker's recovery state: between combo swings it is set before the next blow,
        // and run 40 dropped 30 blocks ~0.15 s in that way (5 of 73 hits blocked).
        if (Intents::WantsDualBlock(a_actor) && !state.target.isAttacking) {
            Intents::EndDualBlockHold(a_actor);
            if (debugEnabled) {
                LOG_DEBUG("DualBlock 0x{:08X}: target stopped attacking, ending the hold", static_cast<std::uint32_t>(formID));
            }
        }
        // The swing this tick is about: tagged on the decision's log lines (Eta, Decision, Intent, Executed)
        t_swingTag = debugEnabled && state.target.isAttacking && state.temporal.target.attackStartTime >= 0.0f
                         ? SwingTag(state.target.targetFormID, state.temporal.target.swingCount)
                         : std::string();
        const struct SwingTagScope
        {
            ~SwingTagScope() { t_swingTag.clear(); }
        } swingTagScope;
        // The player's attack clip predicts the blow (sampled for the clip timing log either way); parries and timed
        // blocks are timed from it, the learned median swing time is the fallback (1.6.x runs: mean miss 0.07 s vs
        // 0.17 s, power attacks 0.07 vs 0.26). timingSource: what this tick's time to the blow came from
        const char *&timingSource = state.temporal.target.timingSource;
        timingSource = "median";
        if (state.target.isPlayer && state.target.isAttacking) {
            if (auto target = RE::TESForm::LookupByID<RE::Actor>(state.target.targetFormID)) {
                const float median = state.temporal.target.timeUntilAttackHits;
                ClipTiming::Sample(target, median, debugEnabled, state.temporal.target.swingCount);
                auto eta = ClipTiming::Eta::kOff;
                float earliest = 0.0f, latest = 0.0f;
                if (config.GetDecisionMatrix().usePlayerClipTiming) {
                    eta = ClipTiming::Predict(target, state.temporal.target.attackStartTime, earliest, latest);
                }
                if (eta == ClipTiming::Eta::kBlow || eta == ClipTiming::Eta::kPast) {
                    timingSource = eta == ClipTiming::Eta::kBlow ? "clip" : "none"; // none: the clip's blows are past
                    state.temporal.target.timeUntilAttackHits = latest;
                    state.temporal.target.timeUntilEarliestHit = earliest;
                }
                if (debugEnabled) {
                    LOG_DEBUG("Eta 0x{:08X}: src={} clip={} atk={} t={:.2f} blow={:.2f} contact={:.2f} median={:.2f} {}",
                              static_cast<std::uint32_t>(formID), timingSource, ClipTiming::Name(eta),
                              static_cast<int>(ActorUtils::SafeGetAttackState(target)),
                              state.temporal.target.attackStartTime,
                              state.temporal.target.timeUntilAttackHits, state.temporal.target.timeUntilEarliestHit,
                              median, t_swingTag);
                }
            }
        }
        if (!t_swingTag.empty()) {
            t_swingTag = std::format("src={} {}", timingSource, t_swingTag);
        }
        MaybeSwitchTarget(a_actor, formID, state);
        DecisionResult decision = m_decisionMatrix.Evaluate(a_actor, state);
        if (debugEnabled) {
            ClipTiming::LogOwnAttack(a_actor, state.target.distance, state.weaponReach, state.target.boundRadius);
        }
        // Every tick, so the watch sees the NPC's own swings and blocks (1.7.7 run: a power attack in place, with no
        // executed decision during it, read as a 4.6 s stall)
        if (debugEnabled) {
            Intents::WatchStall(a_actor, decision.action == ActionType::None ? nullptr : ActionName(decision.action),
                                state.target.distance);
        }
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
        // kBash too: 1.4.1 run, a vanilla bash right after a forward dodge, then a blocked hit, left a bandit in kBash
        // for ~50 s, holding its block, never attacking and never backing off ("no Retreat mid-swing" x249).
        const auto attackState = state.self.attackState;
        if ((attackState == RE::ATTACK_STATE_ENUM::kDraw || attackState == RE::ATTACK_STATE_ENUM::kBash) &&
            state.self.isMelee) {
            const float now = m_time.load(std::memory_order_relaxed);
            float drawSince = now;
            m_drawStart.ModifyOrCreate(formID, [&](AttackStateSince &a_entry) {
                if (a_entry.state != attackState) {
                    a_entry = {attackState, now};
                }
                drawSince = a_entry.since;
            });
            if (now - drawSince > STALE_DRAW_TIMEOUT) {
                if (auto *actorState = ActorUtils::SafeAsActorState(a_actor)) {
                    actorState->actorState1.meleeAttackState = RE::ATTACK_STATE_ENUM::kNone;
                    state.self.attackState = RE::ATTACK_STATE_ENUM::kNone;
                    LOG_DEBUG("Cleared stale {} on 0x{:08X} after {:.1f}s",
                              attackState == RE::ATTACK_STATE_ENUM::kBash ? "kBash" : "kDraw",
                              static_cast<std::uint32_t>(formID), now - drawSince);
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
            LOG_DEBUG("Decision: actor=0x{:08X} action={} priority={:.2f}{}{}", static_cast<std::uint32_t>(formID),
                      ActionName(decision.action), decision.priority, t_swingTag.empty() ? "" : " ", t_swingTag);
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
        // The next-best option the brain had this tick, never an excluded one, never a pull-back mid-swing
        const auto nextBest = [&](auto &&a_excluded) {
            const bool midSwing = state.self.attackState != RE::ATTACK_STATE_ENUM::kNone &&
                                  state.self.attackState != RE::ATTACK_STATE_ENUM::kNextAttack &&
                                  state.self.attackState != RE::ATTACK_STATE_ENUM::kFollowThrough;
            DecisionResult best;
            for (const auto &option : DecisionMatrix::LastCandidates()) {
                const bool pullsBack = option.action == ActionType::Retreat || option.action == ActionType::Backoff;
                if (!a_excluded(option) && option.action != ActionType::None && !(midSwing && pullsBack) &&
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
                                   Personality::CautionScale(Personality::Effective(a_actor).caution);
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
                DecisionResult instead =
                    nextBest([](const DecisionResult &a_option) { return a_option.action == ActionType::Backoff; });
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

        // A failed roll is a mistake, not a freeze: the NPC takes the next-best option it had this tick. That option
        // isn't a free pass: nothing from the failed class (a failed parry no longer turns into a timed block or a
        // dodge), and an option from the other rolled class takes that class's roll (a reflex keeps its own).
        if (decision.action != ActionType::None && !decision.reflex &&
            !m_humanizer.PassesSkillCheck(a_actor, formID, decision.action, state, combatTime)) {
            const ActionType failed = decision.action;
            std::array<bool, 3> classFailed{};
            classFailed[static_cast<std::size_t>(Humanizer::ClassOf(failed))] = true;
            int fallbacksFailed = 0;
            DecisionResult mistake;
            while (true) {
                mistake = nextBest([&](const DecisionResult &a_option) {
                    return a_option.action == failed ||
                           (!a_option.reflex && classFailed[static_cast<std::size_t>(Humanizer::ClassOf(a_option.action))]);
                });
                if (mistake.action == ActionType::None || mistake.reflex ||
                    m_humanizer.PassesSkillCheck(a_actor, formID, mistake.action, state, combatTime)) {
                    break;
                }
                classFailed[static_cast<std::size_t>(Humanizer::ClassOf(mistake.action))] = true; // at most 2 rounds
                ++fallbacksFailed;
            }
            if (debugEnabled) {
                LOG_DEBUG("Skip 0x{:08X}: failed skill roll on {} (skill {:.0f}%, combat time {:.0f}s) - {} instead{}",
                          static_cast<std::uint32_t>(formID), ActionName(failed),
                          m_humanizer.SkillChance(a_actor, failed, combatTime, state) * 100.0f, combatTime,
                          ActionName(mistake.action), fallbacksFailed ? " (the fallback failed its roll too)" : "");
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
        // A boss never waits its turn and takes no minion's slot
        if (Boss::Get(a_actor).boss) {
            coordinator.Release(target, a_formID);
            coordinator.ClearHeld(a_formID);
            return a_decision;
        }
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
        // A wait ring beyond EngageMargin (the Duel preset's far ring) is part of the paced zone out to its far edge:
        // a latecomer stops at the ring instead of walking in, and a waiter isn't released there to walk back in,
        // be held again and fall back out, over and over. The default ring (120) is inside EngageMargin (150).
        // Archers and mages keep the plain zone, or one at range could take a duel's only slot.
        const bool wasHeld = coordinator.IsHeld(a_formID);
        const bool ranged = DecisionMatrix::IsRangedFighter(a_state);
        const float engageMargin = !ranged && pacing.waitDistanceMargin > pacing.engageMargin
                                       ? pacing.waitDistanceMargin + kWaitRingTolerance
                                       : pacing.engageMargin;
        const bool engaging = a_state.target.distance <= reach + engageMargin;
        if (!engaging && (ranged || (!attacking && !commit))) {
            coordinator.Release(target, a_formID);
            coordinator.ClearHeld(a_formID);
            return a_decision;
        }

        const bool debug = Config::GetInstance().IsDebugTarget(a_formID);
        const auto hit = m_retaliation.Find(a_formID);
        const bool retaliating =
            hit && hit->attacker == target && m_time.load(std::memory_order_relaxed) < hit->until;
        // A giant or a werewolf (the player transformed, too) is ganged up on: more slots at once
        const auto danger = TargetDanger::Of(a_actor);
        const int slots = pacing.maxSimultaneousAttackers + (danger.huge || danger.beast ? pacing.dangerExtraAttackers : 0);
        if (retaliating || coordinator.TryAcquire(target, a_formID, slots,
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

        // Wait outside reach: circle on a ring, or step back first if already inside reach. A ring farther out than the
        // tolerance (default ring: reach + 120, so never) is reached by Fallback steps (96-256 units each) first.
        const float holdDistance = reach + pacing.waitDistanceMargin;
        DecisionResult held;
        if (a_state.target.distance <= reach) {
            held.action = ActionType::Backoff;
            held.priority = 1.0f;
            held.paced = true;
        } else if (a_state.target.distance < holdDistance - kWaitRingTolerance) {
            held.action = ActionType::Retreat;
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
        const float powerMin = Boss::PowerStaminaMin(a_actor);
        if (a_state.self.staminaPercent < powerMin) {
            if (executedDecision.action == ActionType::PowerAttack) {
                executedDecision.action = ActionType::Attack;
            } else if (executedDecision.action == ActionType::SprintAttack) {
                executedDecision.action = ActionType::Advancing;
            }
        }
        // A sprint attack launches only from a real sprint inside launch range; until then the NPC advances (the tree's
        // Advance node, which sprints when vanilla decides to)
        if (executedDecision.action == ActionType::SprintAttack && !m_executor.CanLaunchSprintAttack(a_actor, a_state)) {
            executedDecision.action = ActionType::Advancing;
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
            LOG_DEBUG("Executed 0x{:08X}: {} (priority {:.2f}) target 0x{:08X} dist {:.0f} hp {:.0f}%/{:.0f}%{}{}",
                      static_cast<std::uint32_t>(a_formID), ActionName(executedDecision.action),
                      executedDecision.priority, static_cast<std::uint32_t>(a_state.target.targetFormID),
                      a_state.target.distance, a_state.self.healthPercent * 100.0f,
                      a_state.target.healthPercent * 100.0f, t_swingTag.empty() ? "" : " ", t_swingTag);
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

        // Defense timing log: how long before the blow it committed (resolved by the hit event, or "no blow")
        if ((a_state.target.isAttacking || a_state.target.isPowerAttacking) && a_state.temporal.target.attackStartTime >= 0.0f) {
            DefenseTiming::OnCommit(a_actor, a_state.target.targetFormID, executedDecision.action,
                                    a_state.temporal.target.attackStartTime, a_state.temporal.target.timeUntilAttackHits,
                                    a_state.temporal.target.swingCount, a_state.temporal.target.timingSource);
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
        DefenseTiming::Update(a_deltaTime);
        ClipTiming::Update(a_deltaTime);
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
        DefenseTiming::Evict(a_formID);
        ClipTiming::Evict(a_formID);
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
        Potions::Evict(a_formID);
        SelfShouts::Evict(a_formID);
        TargetDanger::Evict(a_formID);
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

        // NPCs (ActorTypeNPC, not ActorTypeCreature), plus creatures that fight like them with a weapon (draugr,
        // falmer, skeletons; IncludeHumanoidCreatures). Animals, trolls, spiders, dragons, giants stay vanilla.
        const bool creature = ActorUtils::SafeHasKeywordString(a_actor, "ActorTypeCreature");
        const bool npc = !creature && ActorUtils::SafeHasKeywordString(a_actor, "ActorTypeNPC") &&
                         ActorUtils::SafeCalculateCachedOwnerIsNPC(a_actor);
        if (!npc) {
            if (!creature || !Config::GetInstance().GetDecisionMatrix().includeHumanoidCreatures ||
                !ActorUtils::IsHumanoidCreature(a_actor)) {
                return false;
            }
            // Once per race: which creature kinds the brain took
            static ThreadSafeMap<RE::FormID, bool> logged;
            const auto *race = a_actor->GetRace();
            const RE::FormID raceID = race ? race->GetFormID() : 0;
            if (!logged.Find(raceID)) {
                logged.ModifyOrCreate(raceID, [](bool &b) { b = true; });
                LOG_INFO("Brain: humanoid creature 0x{:08X} ({}, race 0x{:08X}) - taken, no dodges", a_formID,
                         a_actor->GetName(), raceID);
            }
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
