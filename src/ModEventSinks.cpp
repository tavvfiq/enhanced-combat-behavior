#include "ModEventSinks.h"
#include "ActorUtils.h"
#include "AttackDefenseFeedbackTracker.h"
#include "CombatDirector.h"
#include "Config.h"
#include "GuardCounterFeedbackTracker.h"
#include "Logger.h"
#include "ParryFeedbackTracker.h"
#include "TimedBlockFeedbackTracker.h"
#include "pch.h"

namespace CombatAI
{

    RE::BSEventNotifyControl
    EldenParryEventSink::ProcessEvent(const SKSE::ModCallbackEvent *a_event,
                                      [[maybe_unused]] RE::BSTEventSource<SKSE::ModCallbackEvent> *a_eventSource)
    {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        // Check for melee parry event
        if (a_event->eventName == "EP_MeleeParryEvent") {
            // The sender is the attacker (who got parried) according to EldenParry code
            RE::Actor *attacker = nullptr;
            if (a_event->sender) {
                attacker = a_event->sender->As<RE::Actor>();
            }

            if (attacker) {
                // Notify parry feedback tracker (for when NPC parries)
                ParryFeedbackTracker::GetInstance().OnParrySuccess(attacker);
                // Notify attack defense feedback tracker (for when NPC's attack is parried)
                AttackDefenseFeedbackTracker::GetInstance().OnAttackParried(attacker);
            } else {
                LOG_WARN("EldenParryEventSink: Received EP_MeleeParryEvent but sender is not an Actor");
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl
    TimedBlockEventSink::ProcessEvent(const SKSE::ModCallbackEvent *a_event,
                                      [[maybe_unused]] RE::BSTEventSource<SKSE::ModCallbackEvent> *a_eventSource)
    {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        // Check if this is a Simple Timed Block event
        // STBL_OnTimedBlockDefender - sender is the defender (who successfully timed blocked)
        if (a_event->eventName == "STBL_OnTimedBlockDefender") {
            RE::Actor *defender = nullptr;
            if (a_event->sender) {
                defender = a_event->sender->As<RE::Actor>();
            }

            if (defender) {
                // Notify timed block feedback tracker (for when NPC timed blocks)
                TimedBlockFeedbackTracker::GetInstance().OnTimedBlockSuccess(defender);
            }
        }

        // STBL_OnTimedBlockAttacker - sender is the attacker (whose attack got timed blocked)
        if (a_event->eventName == "STBL_OnTimedBlockAttacker") {
            RE::Actor *attacker = nullptr;
            if (a_event->sender) {
                attacker = a_event->sender->As<RE::Actor>();
            }

            if (attacker) {
                // Notify attack defense feedback tracker (for when NPC's attack is timed blocked)
                AttackDefenseFeedbackTracker::GetInstance().OnAttackTimedBlocked(attacker);
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl
    AttackHitEventSink::ProcessEvent(const RE::TESHitEvent *a_event,
                                     [[maybe_unused]] RE::BSTEventSource<RE::TESHitEvent> *a_eventSource)
    {
        if (!a_event || !a_event->cause || !a_event->target) {
            return RE::BSEventNotifyControl::kContinue;
        }

        // Get attacker and target
        RE::Actor *attacker = a_event->cause->As<RE::Actor>();
        RE::Actor *target = a_event->target->As<RE::Actor>();

        if (!attacker || !target) {
            return RE::BSEventNotifyControl::kContinue;
        }

        // Any hit (player's included, blocked or not) can provoke a target switch
        CombatDirector::GetInstance().OnActorHit(target, attacker);
        AttackDefenseFeedbackTracker::GetInstance().OnRaceHit(attacker, target); // the player's hits count too

        const auto &config = Config::GetInstance();
        if (config.IsDebugTarget(attacker->GetFormID()) || config.IsDebugTarget(target->GetFormID())) {
            LOG_DEBUG("Probe Hit: 0x{:08X} -> 0x{:08X} blocked={} power={}",
                      static_cast<std::uint32_t>(attacker->GetFormID()),
                      static_cast<std::uint32_t>(target->GetFormID()),
                      a_event->flags.any(RE::TESHitEvent::Flag::kHitBlocked),
                      a_event->flags.any(RE::TESHitEvent::Flag::kPowerAttack));
        }

        // Only process if attacker is an NPC (not player)
        // This detects when NPC attacks successfully hit any target (player or NPC)
        if (ActorUtils::SafeIsPlayerRef(attacker)) {
            return RE::BSEventNotifyControl::kContinue; // Skip player attacks
        }

        // Check if hit was blocked - if blocked, it's not a successful hit
        // (parries and timed blocks are handled by mod events, normal blocks are still blocks)
        bool isBlocked = a_event->flags.any(RE::TESHitEvent::Flag::kHitBlocked);
        if (isBlocked) {
            // Not a hit, but not a miss either (run 22: 5 of 17 "misses" in reach were blocked hits)
            AttackDefenseFeedbackTracker::GetInstance().OnAttackBlocked(attacker, target);
            return RE::BSEventNotifyControl::kContinue;
        }

        // This is a successful hit - notify the tracker
        AttackDefenseFeedbackTracker::GetInstance().OnAttackHit(attacker, target);

        // Also check if this was a guard counter attempt (power attack during guard counter window)
        // The guard counter tracker will match this hit with the recorded attempt
        GuardCounterFeedbackTracker::GetInstance().OnGuardCounterHit(attacker);

        return RE::BSEventNotifyControl::kContinue;
    }
} // namespace CombatAI
