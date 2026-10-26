# Enhanced Combat Behavior

An SKSE plugin that gives Skyrim's NPCs a combat brain. It decides when an NPC attacks, blocks,
bashes, parries, dodges, flanks, backs off or retreats, and executes those choices through the game's
own combat behavior trees (vanilla leaf nodes), so animations, attack data and vanilla checks stay in
charge of how each move plays out.

Successor of **Enhanced Combat AI** (`CombatAI-NG.dll`). Not compatible with Enhanced Combat AI, FenixCombatAI
or Combat Pathing Revolution (with CPR installed a startup dialog lets you continue without this mod or exit).

## What it does
- **Decisions every tick per NPC:** offense (attack, power attack, sprint attack, feint, combo
  follow-up), defense (block, bash, parry, timed block, dodge), movement (advance, circle, flank, back
  off, retreat), with level-scaled skill rolls so weaker NPCs make more mistakes.
- **Personality:** each NPC's Confidence, Aggression and combat style set how much risk it takes
  (forward dodges, sprint attacks, trading blows), how careful it is (backing off, blocking, dodging)
  and how patient it is (feints).
- **Group fights:** attack slots (pacing) so a crowd takes turns, flanking near the target, line of
  fire for archers and casters, and no reaction to swings aimed at someone else.
- **Bosses:** bandit chiefs, draugr deathlords and anything matching the boss rules never wait for a
  slot, go through health phases (enraged, desperate), defend better and punish turtling, kiting and
  healing.
- **Humanoid creatures:** draugr, falmer and skeletons with a weapon get the same brain (without dodges).
- **Combo follow-ups for MCO / BFCO movesets** without SCAR.

## Requirements
- SKSE64 for your game version, Address Library for SKSE Plugins.
- Skyrim SE 1.5.97 or AE 1.6.1170 / 1.7.x. Tested in game on AE 1.7.104 only.

## Optional
- **SKSE Menu Framework**: in-game config menu ("Enhanced Combat Behavior").
- **TK Dodge RE**: dodges and forward dodges (without it NPCs don't dodge).
- **Precision**: exact weapon reach.
- **MCO / BFCO** movesets: combo follow-ups. With **SCAR** installed, SCAR drives combos instead and
  this mod only gates them (pacing, defending).
- **Simple Timed Block**, **EldenParry**: timed block / parry windows.
- **powerofthree's Tweaks** with "Load EditorIDs": the "Boss in the EditorID" boss rule.

## Install, update, uninstall
- Install with a mod manager. Files: `SKSE/Plugins/EnhancedCombatBehavior.dll` and
  `SKSE/Plugins/EnhancedCombatBehavior.ini`.
- Update by overwriting. Coming from a development build named CombatAITree: remove `CombatAITree.dll` and
  `CombatAITree.ini`.
- Uninstall any time, mid-playthrough included: no plugin, no scripts, nothing written to saves.

## Compatibility
- **Not compatible:** Enhanced Combat AI, FenixCombatAI (they drive the same combat trees) and Combat
  Pathing Revolution (it patches the same movement code; with CPR installed this mod shows a warning
  dialog at startup and does not load).
- **SCAR:** works; SCAR keeps choosing its attacks, this mod holds them back while an NPC waits its turn
  or defends, and its own combo follow-up switches off.
- **Other parry / timed-block mods:** work if they judge an NPC's vanilla block or bash timing; mods that
  only read the player's key press don't reach NPCs.
- **FluidMotionNPC:** works. While an NPC circles, falls back or backs off (and during its jumps) this mod
  claims it through FluidMotionNPC's API, so FluidMotionNPC doesn't take a deliberate distance for being stuck and
  route, vault or climb it toward its target. Its traversal while an NPC advances is unchanged.
- **Fluid Motion (original, by megakuutti):** not integrated. Its `[Combat] bSwing` auto-attack starts swings
  on its own and overrides this mod's attack timing; set `bSwing = false`.
- **VR:** not supported.

## Configure
Edit `Data/SKSE/Plugins/EnhancedCombatBehavior.ini` (every key is commented) or use the in-game menu,
which saves back to the INI. The menu has five tabs (General, Offense, Defense, Groups & Movement, NPC
Character) with the rarely changed settings under "Advanced" headers. Presets: Default, Vanilla+ (gentle),
Souls-like (sharp duels), Chaos (a swarm); each resets tuning to the defaults first.

## Debug log
`Documents/My Games/Skyrim Special Edition/SKSE/EnhancedCombatBehavior.log`. Off by default
(`[General] EnableDebugLog`). With it on, select an NPC in the console to follow its decisions.

## Build
- xmake 3.0+, MSVC (Visual Studio 2022 or newer).
- CommonLibSSE-NG checkout: set `COMMONLIBSSE_NG_DIR` to it.
- `xmake f -m release --skyrim_se=y --skyrim_ae=y --skyrim_vr=n -y`, then `xmake -y`.
- `pwsh scripts/package_release.ps1 <version>` builds the release zip under `releases/`.

Design notes: `docs/TREE_DESIGN.md`. Changes: `CHANGELOG.md`.

## License
GPL-3.0-or-later (`LICENSE`); it links CommonLibSSE-NG, which is GPL-3.0-or-later. Third-party notices:
`THIRD_PARTY.md`. Credits: `CREDITS.md`.
