# STATE - CombatAITree

**Goal:** EnhancedCombatAI's brain (DecisionMatrix, skill rolls, cooldowns) driving vanilla leaf
nodes through our own behavior trees, instead of raw animation events. Design: `TREE_DESIGN.md`.

**Owner folder:** D:\Modding\combat-ai-tree (git). Research and tools: D:\Modding\skyrim_re.

**Active version:** 0.5.1 (CURRENT.txt). Releases: `releases\<version>\CombatAITree-<version>.zip`, one per
version so any step can be rolled back.

**Runtime status:**
- 0.1.0 ran 7 min clean. 0.2.0 hung the game (tree cycle), fixed in 0.2.1. 0.2.1 and 0.2.2 ran clean
  (no hang, movement through the tree works without CPR).
- 0.3.0 (includes 0.2.3/0.2.4), run 4: ~4 min against 3 bandits, no hang. The bash intents were all
  taken (115/114). Movement intents ran. Vanilla attacks ran in the tree. **The brain still never picked
  an attack** (0 Attack candidates in 1508 decisions). Feint reset the attack timer (fixed in 0.3.1).
  288 in-range ticks fail on a gate we can't yet see; 0.3.1 logs which one.
- 0.3.1, run 5 (~1 min): no errors. Every in-range offense skip was `line of sight blocked` (102/102):
  the kLOS ray hits bodies. 0.3.2 ignores actor hits.
- 0.3.2, run 6 (~2.7 min): no warnings. **Brain attacks work:** light 20 posted/20 taken/18 with attack data,
  power 5/5/4. Offense skips are only the intended ones (116 within 0.3 s of its own swing, 55 target swinging).
  The skill roll dropped 58 of 70 Attack decisions (skill 33-55% early in the fight), by design. LOS blocks 30, all
  unnamed static refs. Circle side: 46 lines, not yet checked by eye.
- 0.4.0 (movement preemption): build-validated only.
- 0.5.0 (arc jump after FluidMotionNPC, own code): build-validated only.
- 0.5.1 (config menu visible in SKSE Menu Framework as Combat AI Tree / Config): build-validated only.

**Next (for the user):**
1. Install 0.5.1 (includes 0.4.0 and 0.5.0). Check the menu: Combat AI Tree / Config in SKSE Menu Framework. Fight a bandit camp with archers and one bandit selected. Check:
   - **0.4.0 movement:** the summary's `running moves stopped: ours N, vanilla N` is above 0, no foot stutter (no
     `Move ... stopped running vanilla node` for the same actor every 0.3 s), no hang;
   - **0.5.0 jump:** set `EnableJumpEvasion` / `EvasionJumpChance` high in the INI to see it often. Look for
     `Jump 0x..: side arc ...` then `landed`, and watch whether the leap looks right: no sliding pose, no fall
     damage, no bounce, no stuck jump state;
   - **circle side** by eye against the `Circle ... (brain side +1)` line (+1 = to its own left).
   - If 0.5.0 misbehaves, 0.4.0 is the same build without the jump change.
2. Then (dev): Surround + fallback distance parameters. The custom leaf is parked (TREE_DESIGN §6).
**Last good version:** 0.3.2 (runtime-evidenced, run 6). Then 0.3.1 (run 5), 0.3.0 (run 4). Before that 0.2.2, then EnhancedCombatAI 1.10.2.
**Log:** `Documents\My Games\Skyrim Special Edition\SKSE\CombatAITree.log`
(debug lines need `EnableDebugLog = true` under `[General]` in `CombatAITree.ini`, the shipped default;
they cover the actor selected in the console).

**Build:** `xmake f -m releasedbg --skyrim_se=y --skyrim_ae=y --skyrim_vr=n -y`, `xmake -y`, then
`pwsh scripts/package_release.ps1 <version>`. Output DLL: `build/SkyrimSEAE/skse/plugins/CombatAITree.dll`.