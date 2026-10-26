#pragma once

#include "pch.h"

// Boss detection and phases. Bosses never wait for an attack slot, defend and roll their skill better, follow their
// health through phases (normal > 50%, enraged below 50%, desperate below 25%) and punish turtling, kiting and healing
// (DecisionMatrix::EvaluateBoss).
// A boss is an NPC whose placed reference carries vanilla's "Boss" location ref type (Skyrim.esm LCRT 0x000130F7,
// EditorID "Boss", put on dungeon bosses such as bandit chiefs), or whose name is in [Boss] BossNames
// (default "Bandit Chief, Draugr Deathlord": deathlords also appear as plain elites without the marker), or whose
// NPC record, template or leveled list has "Boss" in its EditorID (Skyrim.esm: 357 NPC_ and 80 LVLN, e.g.
// LvlBanditBossMelee2HNordM, LCharDraugrBossNoDragonPriest). The game drops most EditorIDs at load; that rule only
// works when a mod keeps them (powerofthree's Tweaks "Load EditorIDs"), which is checked and logged once.
namespace CombatAI::Boss
{
    struct Info
    {
        bool boss = false;
        bool marker = false; // the Boss location ref type
        bool named = false;  // name in BossNames
        bool editorID = false; // "Boss" in the EditorID of its NPC, a template or its leveled list
        std::string match;     // that EditorID
    };

    // Cached per actor; logs each NPC's verdict once (info for bosses, debug for the selected NPC otherwise)
    Info Get(RE::Actor *a_actor);

    enum class Phase
    {
        None, // not a boss
        Normal,
        Enraged,  // below 50% health: more risk, power attacks and combo follow-ups
        Desperate // below 25%: a brave boss goes all-in, a careful one falls back among its minions
    };
    // Brain tick: the phase from its health (logs each change); None for non-bosses
    Phase UpdatePhase(RE::Actor *a_actor, float a_healthPercent);
    // The phase the last UpdatePhase set (None for non-bosses)
    Phase CurrentPhase(RE::Actor *a_actor);
    // A brave boss (Confidence Brave or Foolhardy) goes all-in when desperate, others fall back
    bool Brave(RE::Actor *a_actor);
} // namespace CombatAI::Boss
