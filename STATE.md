# STATE - CombatAITree

**Goal:** EnhancedCombatAI's brain (DecisionMatrix, skill rolls, cooldowns) driving vanilla leaf
nodes through our own behavior trees, instead of raw animation events. Design: `TREE_DESIGN.md`.

**Owner folder:** D:\Modding\combat-ai-tree (git). Research and tools: D:\Modding\skyrim_re.

**Active version:** 0.3.0 (CURRENT.txt). Releases: `releases\<version>\CombatAITree-<version>.zip`, one per
version so any step can be rolled back.

**Runtime status:**
- 0.1.0 ran 7 min clean. 0.2.0 hung the game (tree cycle), fixed in 0.2.1. 0.2.1 and 0.2.2 ran clean
  (no hang, movement through the tree works without CPR).
- 0.2.3 / 0.2.4 / 0.3.0: build-validated only.

**Next (for the user, in order):**
1. Install 0.3.0 and fight a bandit camp with the debug NPC selected. Check: brain attacks now
   (`post attack`, `tree took attack`, the `Intents (last 60s ...)` summary), the `Circle ...` side line
   matches the side the bandit circles, no hang. If anything is wrong, step back through 0.2.4 / 0.2.3.
2. Then (dev): movement preemption probe (TREE_DESIGN §9), own leaf class (§6), Surround + fallback
   distance parameters.

**Last good version:** 0.2.2 (ran clean in game; bandits attack, movement works). Rollback before that:
EnhancedCombatAI 1.10.2.

**Log:** `Documents\My Games\Skyrim Special Edition\SKSE\CombatAITree.log`
(debug lines need `EnableDebugLog = true` under `[General]` in `CombatAITree.ini`, the shipped default;
they cover the actor selected in the console).

**Build:** `xmake f -m releasedbg --skyrim_se=y --skyrim_ae=y --skyrim_vr=n -y`, `xmake -y`, then
`pwsh scripts/package_release.ps1 <version>`. Output DLL: `build/SkyrimSEAE/skse/plugins/CombatAITree.dll`.