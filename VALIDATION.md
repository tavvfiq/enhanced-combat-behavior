# VALIDATION - CombatAITree

| Version | Check | Result |
|---|---|---|
| 0.1.0 | Baseline fork builds unchanged (xmake releasedbg, SE+AE) | pass (tool-validated) |
| 0.1.0 | Fenix-NG node/tree IDs: AE 1.6.1170 vs 1.7.104 normalized-instruction diff; SE vtable RTTI | pass, except `isMovingFrom` (unused in 0.1.0) |
| 0.1.0 | Vanilla chance-function IDs SE->AE (49747/50674 .. 49756/50683) from both tree builders | pass (tool-validated) |
| 0.1.0 | Build with tree layer | pass, no new warnings |
| 0.1.0 | In game, AE 1.7.104, 7 min, 3 marauders + Tsun, no CPR/Precision/TK Dodge | loads, both trees built once, no errors; vanilla Attack branch runs StartAttack (attack-chance hook 6 candidates x 110 / x 48); bash intent taken 36 ms after post. Regression: Feint re-posted bash every tick (fixed in 0.1.1). Attack/Parry/TimedBlock intents not exercised (brain chose none; Parry/TimedBlock off in INI) |
| 0.1.1 | Build | pass |
| 0.1.1 | In game | not run yet |
| 0.2.0 | CloseMovement SetRoot call site: one E8 to initNodes in SE 46731 (+0x512), AE 1.6.1170 and 1.7.104 47928 (+0xE9C) | pass (tool-validated) |
| 0.2.0 | Build | pass |
| 0.2.0 | In game | **FAIL**: game hangs when a hostile bandit is spawned (combat start); spawned ally Tsun fine. Cause found statically: tree cycle (AddChildren climbs parents), fixed in 0.2.1 |
| 0.2.1 | Build | pass |
| 0.2.1 | In game, AE 1.7.104, 2.5 min, 3 bandits at a camp, no CPR | no hang; 754 movement decisions executed (0 in 0.1.0 without CPR), 72 intent changes, positions follow; bash 6 posted / 6 taken; vanilla blocks present. Problem: bandits passive - "our defense" veto blocked 299/440 attack checks after Backoff/Strafe/Retreat (stamina 0 after a vanilla power attack -> brain offers only Retreat/Backoff). Veto removed in 0.2.2 |
| 0.2.2 | Build | pass |
| 0.2.2 | In game, 5.5 min, camp bandits + spawned marauder + Tsun | no hang; our movement branches entered 24x (Advance 7 / Circle 6 / Fallback 4 / Backoff 7); bash 23 posted / 21 taken; veto gone (784 unchanged, 17 pacing); attack-state snapshots 13% (6% in 0.2.1). Brain chose Attack 0x: offense gated by timeSinceLastAction reset by locomotion (fixed 0.2.3) |
| 0.2.3 | Build | pass |
| 0.2.3 | In game | not run yet: expect `Intent ... post attack 1/2` and `tree took attack` lines |
| 0.2.4 | Circle angle call site: SE 46720+0x299, AE 1.6.1170 / 1.7.104 47916+0x22B -> getter 49721 / 50648 (E8) | pass (tool-validated) |
| 0.2.4 | Build | pass |
| 0.2.4 | In game | not run yet: check a `Circle ...` debug line against the side the bandit actually circles (brain side +1 should be the bandit's left) |

## In-game test for 0.2.0 (no CPR)
1. Log: `Tree: CloseMovement SetRoot hooked` at load, `Tree: CloseMovement tree extended` when the first fight starts. If `unexpected shape` or `site does not match` appears, report it.
2. Select a bandit in the console and fight it: debug `Intent ... post move 1..4` lines, and the NPC visibly circles, backs off, falls back and closes in (it did nothing for these decisions in the 0.1.0 run).
3. Watch for NPCs that circle forever and never swing (the defense veto that caused this in 0.2.1 is gone in 0.2.2).
4. `tree runs move 1..4` lines show when our branch (not vanilla's) is moving the NPC.

## In-game test for 0.1.0 (AE 1.7.104)
1. Check `EnableDebugLog = true` under `[General]` in CombatAITree.ini (shipped default). Load a save, open the console, click a bandit, close the console.
2. Log has `Tree: Melee tree built` and `Tree: Block tree built` (once each), no errors.
3. Fight the bandit for 3+ minutes. Expect: it still attacks, power attacks, blocks and bashes
   (vanilla branches); debug lines `Intent ... post attack` followed by `tree took attack` and
   `VanillaAttack ... (intent)`, and the same for bash/block.
4. Fight a group (2-3 bandits + a follower), then a mage and an archer: no freezes, no CTD.
5. Save in combat, reload: combat resumes, no CTD.
6. Report: any NPC that stops attacking or never blocks, and the log.