# Credits

## Built on
- **EnhancedCombatAI** (Enhanced Combat AI, `CombatAI-NG.dll`): this mod is its successor. The decision
  code (state observers, decision matrix, skill rolls, config and menu) started there. MIT, see
  `THIRD_PARTY.md`.
- **FenixCombatAI-NG** (tavvfiq's port of FenixCombatAI): the approach of building combat behavior
  trees out of the engine's own node classes, and the node IDs in `include/TreeNodes.h`.
- **Combat Pathing Revolution** by max-su-2019: behavior-tree structure layouts (via FenixCombatAI-NG).
  MIT.
- **SCAR** by max-su-2019: the `SCAR_ActionData` format and range rules, and how SCAR starts an attack.
  MIT.

## Research references
- **ProjectGapClose**: hook patterns and sprint-attack rules.
- **KeyLogicExpansion** (Acook1e): how MCO / BFCO combo windows are read for the player.
- **MCO**, **ADXP MCO Bug Fixes**, **BFCO** and the **MCO to BFCO Converter** documentation on Nexus:
  combo window annotation names.
- **SimpleDualWieldBlock** (WastelPlaster): the player-side dual-wield block idea.

## Libraries
- CommonLibSSE-NG (alandtse and contributors), spdlog, SimpleIni.
- SKSE Menu Framework (in-game config menu API).
- Precision API (Ersh).

## Thanks
The Skyrim modding community for its tools, frameworks and reverse-engineering notes.
