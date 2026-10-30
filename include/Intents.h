#pragma once

#include "pch.h"
#include <chrono>

// Intent board: the brain (CombatDirector, Character::Update thread) posts what an actor should do
// next; the behavior-tree conditions (engine combat jobs, parallel tree threads) read it. Intents
// are one-shot: a tree branch consumes one when it takes it, and an unread one expires on its own,
// so a stale decision never fires late.
namespace CombatAI::Intents
{
    enum class Attack : std::uint8_t
    {
        None,
        Light,
        Power
    };

    enum class Defend : std::uint8_t
    {
        None,
        Block,
        Bash
    };

    // Movement node our CloseMovement branches run. Unlike attack/defend this is a state, not a
    // one-shot: the brain refreshes it every decision and the branch runs while it holds.
    enum class Move : std::uint8_t
    {
        None,
        Advance,
        Circle,
        Fallback,
        Backoff
    };

    // Brain side. False if the same intent is still pending: the tree hasn't been able to run it yet,
    // so the brain must not count it as executed again (the old raw events failed the same way).
    bool PostAttack(RE::Actor *a_actor, Attack a_kind);
    bool PostDefend(RE::Actor *a_actor, Defend a_kind);

    // Tree side. TakeAttack returns and consumes the pending attack, and commits its kind for the
    // attack-chance hook, which runs a moment later inside the Attack node. TakeDefend consumes
    // the pending defense only if it is a_kind (bash and block are separate tree branches).
    Attack TakeAttack(RE::Actor *a_actor);
    bool TakeDefend(RE::Actor *a_actor, Defend a_kind);

    // Tree side, debug: one of our trees read the board for a_actor (selected NPC only). An attack or defense that
    // expires untaken logs the reads since it was posted (1.7.4 run: an NPC stood 13 s at 66 units, its attack
    // re-posted every 0.55 s, never taken, no tree activity logged)
    enum class TreeRead : std::uint8_t
    {
        Melee,
        Block,
        Move, // CloseMovement
        Magic,
        RangedMove // RangedMovement
    };
    void NoteTreeRead(RE::Actor *a_actor, TreeRead a_tree);

    // Debug, brain side (selected NPC): call every tick with the brain's action (nullptr: none). Standing still 2 s
    // while the brain wants an action (not swinging, blocking or staggered) logs a Stall line: the NPC's state, our
    // trees' reads, the combat nodes running and what stands between it and its target (1.7.4 run: "Advancing" for
    // 3-4 s at a flat distance; no attack posted, so no untaken line)
    void WatchStall(RE::Actor *a_actor, const char *a_action, float a_distance);
    // Brain side, movement: (re)start the movement intent; always true (refreshing is expected).
    // a_param: Circle: +1 circle left, -1 right, 0 no preference. Advance: distance to the target to stop at
    // (the brain's attack range; 0 = vanilla's). Unused for the other moves.
    bool PostMove(RE::Actor *a_actor, Move a_kind, float a_param = 0.0f);
    // Drop any movement intent (e.g. no backing off mid-swing).
    void ClearMove(RE::Actor *a_actor);
    // Tree side, movement: the brain currently wants a_kind (not consumed).
    bool WantsMove(RE::Actor *a_actor, Move a_kind);
    // Tree side, movement: the move the brain currently wants (None once the intent expired).
    Move WantedMove(RE::Actor *a_actor);
    // A running movement node was stopped because the brain wants something else (summary count).
    void NotePreempted(bool a_vanillaNode);
    // Side the brain wants the running Circle node to take (+1 left, -1 right, 0 vanilla's choice).
    float CircleSide(RE::Actor *a_actor);
    // Tree side: our Circle node failed (start or run) while the brain wanted a side. Brain side: that side
    // (+1/-1) failed under 2 s ago, so try the other one.
    void NoteCircleRefused(RE::Actor *a_actor);
    // Brain side: it is defending (dodge, block, back off, retreat); attacks that bypass the tree's intents (SCAR)
    // are refused until a_seconds from now. Refreshed while the brain keeps defending.
    void PostAttackVeto(RE::Actor *a_actor, float a_seconds);
    // An attack or bash start event was accepted by the actor's graph (EngineProbes); seconds since then (999 = none)
    void NoteSwingStarted(RE::Actor *a_actor);
    float SinceSwing(RE::Actor *a_actor);
    bool AttackVetoed(RE::Actor *a_actor);
    bool CircleRefused(RE::Actor *a_actor, float a_side);
    // Our Circle failed on either side under 2 s ago
    bool CircleRefusedRecently(RE::Actor *a_actor);
    // Distance the brain wants the running Advance to stop at (0 = vanilla's own).
    float AdvanceDistance(RE::Actor *a_actor);

    // Its last 3 block/bash requests expired untaken, the latest under 10 s ago: its Block tree isn't running
    // (run 29: a dagger bandit's 11 bash posts, 0 taken, while it feinted 75 times). The brain stops asking.
    bool DefendUnavailable(RE::Actor *a_actor);

    // Direct block (DualBlock tree node) for NPCs without the Block tree. Unlike block/bash it is a state: the
    // node holds while the brain's hold lasts. Start/End return true once, for the caller to send the events.
    void PostDualBlock(RE::Actor *a_actor, float a_seconds);
    // The swing it guards against is over: end the hold now (the tree releases on its next pass)
    void EndDualBlockHold(RE::Actor *a_actor);
    bool WantsDualBlock(RE::Actor *a_actor);
    bool StartDualBlock(RE::Actor *a_actor);
    bool EndDualBlock(RE::Actor *a_actor, bool a_force);
    bool DualBlockStarted(RE::FormID a_formID);
    // Started and the hold ended over 0.3 s ago without the tree releasing it (thread gone): release from the brain
    bool DualBlockOverdue(RE::Actor *a_actor);

    // Archers: how much more (or less) likely vanilla's ranged attack roll should be this moment (0 = hold the shot,
    // 1 = vanilla, 2 = take it). Refreshed every brain tick, falls back to 1 after 1 s. A hold (0) lasts 3 s at
    // most, then 1.5 s of vanilla shots before the next one; returns the factor actually posted.
    float PostRangedShot(RE::Actor *a_actor, float a_factor);
    float RangedShotFactor(RE::Actor *a_actor);

    // Potions: vanilla wants a drink but the target is too close. Each refused drink keeps the wait 1 s (the brain
    // retreats meanwhile, see WantsToDrink); returns how long this wait has lasted (s).
    float PostDrinkWait(RE::Actor *a_actor);
    void EndDrinkWait(RE::Actor *a_actor);
    bool WantsToDrink(RE::Actor *a_actor);

    // Pending (not consumed) attack/defend intents exist - keeps vanilla branches out of the way.
    bool HasPending(RE::Actor *a_actor);

    // Attack kind the Attack node is currently choosing attack data for (None = vanilla choice).
    Attack CommittedAttack(RE::Actor *a_actor);

    // Brain side, each tick: the NPC's weapon reach plus its target's body radius (center to center), and the tree
    // side's read of it (0 = never noted). The hit cone override (EngineProbes) checks our attacks against it
    void NoteAttackReach(RE::Actor *a_actor, float a_reach);
    float AttackReach(RE::Actor *a_actor);

    // Attack-chance hook found attack data of the committed kind (feedback: the intent really ran).
    void NoteAttackMatched(RE::Actor *a_actor);

    // Feedback summary (posted / taken / attack data matched per kind) at info level; call once
    // per frame from the main thread, it logs at most once a minute and only if something happened.
    void LogSummary(float a_deltaTime);

    void Evict(RE::FormID a_formID);
} // namespace CombatAI::Intents
