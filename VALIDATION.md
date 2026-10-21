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

## In-game test for 0.1.0 (AE 1.7.104)
1. Check `EnableDebugLog = true` under `[General]` in CombatAITree.ini (shipped default). Load a save, open the console, click a bandit, close the console.
2. Log has `Tree: Melee tree built` and `Tree: Block tree built` (once each), no errors.
3. Fight the bandit for 3+ minutes. Expect: it still attacks, power attacks, blocks and bashes
   (vanilla branches); debug lines `Intent ... post attack` followed by `tree took attack` and
   `VanillaAttack ... (intent)`, and the same for bash/block.
4. Fight a group (2-3 bandits + a follower), then a mage and an archer: no freezes, no CTD.
5. Save in combat, reload: combat resumes, no CTD.
6. Report: any NPC that stops attacking or never blocks, and the log.