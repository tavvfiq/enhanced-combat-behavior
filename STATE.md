# STATE - CombatAITree

**Goal:** EnhancedCombatAI's brain (DecisionMatrix, skill rolls, cooldowns) driving vanilla leaf
nodes through our own behavior trees, instead of raw animation events. Design: `TREE_DESIGN.md`.

**Owner folder:** D:\Modding\combat-ai-tree (git). Research and tools: D:\Modding\skyrim_re.

**Done (0.1.0 ran in game without errors; 0.1.1/0.2.0 build-validated):**
- 0.2.0: movement intents spliced into the vanilla CloseMovement tree (Advance/Circle/Fallback/Backoff), no CPR needed.
- Fork baseline commit `0e94a1b` = combat-ai-ng 1.10.2 working tree.
- Melee + Block trees (`src/Trees.cpp`), node layer (`src/TreeNodes.cpp`), intent board
  (`src/Intents.cpp`), executor attack/bash/block rerouted, attack-chance hook honours intents.

**Next:**
1. In-game test (see VALIDATION.md), with the debug log on and the test NPC selected in the console.
2. 0.2.x: movement preemption (RE vanilla DynamicSelector) and parameters via getter hooks (circle side/radius, fallback distance).
3. 0.3.0: feedback from leaf nodes to the brain.

**Last good version:** none yet. Rollback = original EnhancedCombatAI 1.10.2.

**Log:** `Documents\My Games\Skyrim Special Edition\SKSE\CombatAITree.log`
(debug lines need `EnableDebugLog = true` under `[General]` in `CombatAITree.ini`, the shipped default; they cover the actor selected in the console).

**Build:** `xmake f -m releasedbg --skyrim_se=y --skyrim_ae=y --skyrim_vr=n -y` then `xmake -y`
(or `BuildRelease.bat`). Output: `build/SkyrimSEAE/skse/plugins/CombatAITree.dll`.