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
    // Brain side, movement: (re)start the movement intent; always true (refreshing is expected).
    // a_circleSide: +1 circle left, -1 right, 0 no preference (only used with Move::Circle).
    bool PostMove(RE::Actor *a_actor, Move a_kind, float a_circleSide = 0.0f);
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

    // Pending (not consumed) attack/defend intents exist - keeps vanilla branches out of the way.
    bool HasPending(RE::Actor *a_actor);

    // Attack kind the Attack node is currently choosing attack data for (None = vanilla choice).
    Attack CommittedAttack(RE::Actor *a_actor);

    // Attack-chance hook found attack data of the committed kind (feedback: the intent really ran).
    void NoteAttackMatched(RE::Actor *a_actor);

    // Feedback summary (posted / taken / attack data matched per kind) at info level; call once
    // per frame from the main thread, it logs at most once a minute and only if something happened.
    void LogSummary(float a_deltaTime);

    void Evict(RE::FormID a_formID);
} // namespace CombatAI::Intents
