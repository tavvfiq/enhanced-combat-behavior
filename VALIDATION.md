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
| 0.3.0 | Build | pass |
| 0.3.0 | In game | not run yet: read the `Intents (last 60s ...)` lines |

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
## 0.3.1 (2026-10-22)
- Build: xmake releasedbg SE+AE, ok (pre-existing C4100 warnings only). DLL FileVersion 0.3.1.0.
- Ship-gate validator: PASS. Zip sha256 99CE7810FE2A81413318A60A075E0DFCF1CDA257F565715DE6003338A2EBD1A3.
- Runtime: untested. 0.3.0 runtime-evidenced clean in run 4 (log 2026-10-22 06:43-06:47).

## 0.3.2 (2026-10-22)
- IDs: FindCollidableRef 25466/26003 resolves on SE, AE 1.6.1170, 1.7.104 (pdata function start; AE/1.7.104 bodies identical, 36 insns) via skyrim_re/re/fenixport.py.
- Build: xmake releasedbg SE+AE ok; DLL FileVersion 0.3.2.0. Ship-gate validator PASS. Zip sha256 E4F28B00FD3CEFD21256C22CA4B03D41FFA53F2F08D02C0155E89A6802BB250B.
- Runtime: untested. 0.3.1 runtime-evidenced (run 5, log 06:53-06:54): LOS gate blocked 102/102 in-range offense checks.
- 0.3.2 runtime (run 6, log 07:02-07:05): 0 warnings/errors; Intents light 11/11/11 + 9/9/7, power 1/1/1 + 4/4/3, bash 40/40; offense skips: 116 own-attack recency, 55 target attacking; LOS blocks 30 (statics).

## 0.4.0 (2026-10-22)
- RE: Thread::Update 46228 slot semantics (SE disasm); slot 5 of Advance/Circle/Fallback/Backoff = TLS-thread Ascend and Ascend body identical on SE 46229 / AE 47484 (1.6.1170 and 1.7.104) via re/se.py; vtables RTTI-named CombatBehaviorTreeNodeObject<CombatBehavior{Advance,Circle,Fallback,Backoff}> on all three.
- Build: xmake releasedbg SE+AE ok; DLL FileVersion 0.4.0.0. Ship-gate validator PASS. Zip sha256 4EE48BFB3C02C4FFBBA91EFC71DF29D6CABC24AE2C8D194E4E00EDC5709EB72C.
- Runtime: untested.

## 0.5.0 (2026-10-22)
- Build: xmake releasedbg SE+AE ok; DLL FileVersion 0.5.0.0. Ship-gate validator PASS. Zip sha256 D8D291631585BDA72AB0B81641CD5D12F9A460315E7F220F805F90061C76DACB.
- Source: method from FluidMotionNPC JumpNav (read only); no GPL code copied. FMNPC exports FMNPC_IsTraversing(uint32)->bool and FMNPC_Claim(uint32,float) per its src/Api.cpp @8402947.
- Runtime: untested.
