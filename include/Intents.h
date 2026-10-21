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

    // Brain side. False if the same intent is still pending: the tree hasn't been able to run it yet,
    // so the brain must not count it as executed again (the old raw events failed the same way).
    bool PostAttack(RE::Actor *a_actor, Attack a_kind);
    bool PostDefend(RE::Actor *a_actor, Defend a_kind);

    // Tree side. TakeAttack returns and consumes the pending attack, and commits its kind for the
    // attack-chance hook, which runs a moment later inside the Attack node. TakeDefend consumes
    // the pending defense only if it is a_kind (bash and block are separate tree branches).
    Attack TakeAttack(RE::Actor *a_actor);
    bool TakeDefend(RE::Actor *a_actor, Defend a_kind);
    // Pending (not consumed) intents exist - used to keep vanilla branches out of the way.
    bool HasPending(RE::Actor *a_actor);

    // Attack kind the Attack node is currently choosing attack data for (None = vanilla choice).
    Attack CommittedAttack(RE::Actor *a_actor);

    void Evict(RE::FormID a_formID);
} // namespace CombatAI::Intents
