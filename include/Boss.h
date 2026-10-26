#pragma once

#include "pch.h"

// Boss detection (step 1 of boss handling: detection and logging only, no behavior change yet).
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
} // namespace CombatAI::Boss
