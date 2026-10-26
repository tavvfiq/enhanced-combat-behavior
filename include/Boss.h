#pragma once

#include "pch.h"

// Boss detection (step 1 of boss handling: detection and logging only, no behavior change yet).
// A boss is an NPC whose placed reference carries vanilla's "Boss" location ref type (Skyrim.esm LCRT 0x000130F7,
// EditorID "Boss", put on dungeon bosses such as bandit chiefs), or whose name is in [Boss] BossNames
// (default "Bandit Chief, Draugr Deathlord": deathlords also appear as plain elites without the marker).
namespace CombatAI::Boss
{
    struct Info
    {
        bool boss = false;
        bool marker = false; // the Boss location ref type
        bool named = false;  // name in BossNames
    };

    // Cached per actor; logs each NPC's verdict once (info for bosses, debug for the selected NPC otherwise)
    Info Get(RE::Actor *a_actor);
} // namespace CombatAI::Boss
