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

## 0.5.1 (2026-10-22)
- Cause: extern/styyx-util SKSEMenuFramework.h resolves GetModuleHandleW(SKSEMenuFramework) at static init; log of run 6 shows 'Registered' while user reports no menu entry. Fix re-resolves at kDataLoaded.
- Build ok; FileVersion 0.5.1.0; ship-gate PASS. Runtime: untested.

## 0.4.0-0.5.1 run 7 (2026-10-22, user log, 0.5.1 build, ~3 min vs 2 bandits, player melee)
- Loads clean, no warnings/errors, no crash log. Menu: config saved from the SKSE Menu Framework window twice (09:17:04, 09:17:46), so 0.5.1 menu fix is **runtime-evidenced**.
- Preemption: summary `running moves stopped: ours 24/40, vanilla 181/238` per minute. **FAIL:** 0x8E1A0 stopped vanilla Advance every frame (12 ms) while the brain wanted Circle at ~900 units (vanilla K1 want2: 88 logged lines). Fixed in 0.5.2.
- Jump: 0 jump decisions. Not a fault: jump evasion only fires when the *target* (player) holds a bow aimed at the NPC; the player was melee all run (`Target: Ranged=false`). Untested.
- Dodge: 117 TKDodge* events, all `rejected` by the NPC graph (no NPC TK Dodge behavior in the load order, or wrong event names). ForwardDodge falls back to Advance; plain Dodge leaves vanilla in charge.
- Idle in melee: 0x8E1A1 (Precision reach 48) executed Advancing 99 times while standing still at 60-100 units; attack range was 52. Fixed in 0.5.2.

## 0.5.2 (2026-10-22)
- Build ok (static_asserts on the stop rule compile); FileVersion 0.5.2.0; ship-gate PASS. Zip sha256 E48B8A60077A4B1F553F9114DCB9B791B217672DA7F0B987C89DC02025D5D49A. Runtime: untested.

## 0.5.2 run 8 (2026-10-22, user log, ~5 min vs 3 bandits, player bow + melee)
- Loads clean, no warnings/errors; game still running at analysis (no crash). The 09:21 crash log is the earlier session's exit: BetterAltTab.dll WindowProc null call, CombatAITree not on the stack.
- Preemption fix: **runtime-evidenced**. Vanilla stops 32/32/22/2 per minute (238 in run 7); shortest gap between two stops of one actor 316 ms (0 under 300 ms).
- Reach fix: reach now 72/82 (48/58 + 24); short-weapon 0x8E1A1 in range took Attack 28 / PowerAttack 18 decisions. Light 10/4/4/3 and power 4/4/1 posted and taken per minute.
- Jump: 0 decisions. All jump conditions held on 31 ticks (bow aimed, idle, 500-1500 units), all before the 09:45:08 menu save; shipped INI default `EnableJumpEvasion = false`, so jump evasion was most likely off. Untested.
- Offense skips: "target is attacking" 115, 92 of them with the player holding a bow. Fixed in 0.5.3.
- Dodge: 68 TKDodge events, all rejected again (no NPC TK Dodge behavior or wrong events).
- Low-HP bandit 0x8E1A4 (20-39% HP) retreated 345 ticks; Fallback posted 16 / run 7; bash (defend 2) posted 4 / taken 0. Not changed.

## 0.5.3 (2026-10-22)
- Build ok; FileVersion 0.5.3.0; ship-gate PASS. Zip sha256 DEAFBB0E3C2590A6A8236459AEA3A041408607C13C8619E3B270715663EBEE1C. Runtime: untested.

## 0.5.3 run 9 (2026-10-22, user log, ~3.5 min vs bandits, player mostly bow)
- No warnings/errors. Log shows `Jump evasion: OFF` at load, `on` after the 09:55:27 menu save (confirms the run 8 diagnosis).
- Crash 09:57:56 (8 s after the last log line): BetterAltTab.dll+0x25259 WindowProc null call during `Xaudio::stopEngine`, no CombatAITree frame. Same signature as 09:21 and 09:50. Not ours.
- Jump: **runtime-evidenced.** 47 Jump decisions: 34 failed the skill roll, 5 on cooldown, 5 executed; the rest arrived mid-jump and were ignored. All 5 were side arcs of 150 units (landing dz -31..+2, 0.94-1.01 s), and all 5 logged `landed`. No aborts, no "no clear arc", no reland retry. Not yet checked by eye.
- Bow gate fix: **runtime-evidenced.** "target is attacking" skips 29, all with a melee target (0 while the player held a bow; 92 in run 8).
- Attack skill roll dropped 81 Attack decisions (skill 33-55%, by design).

## 0.6.0 (2026-10-22)
- User feedback on run 9: jump facing reads fine (sideways leap while facing the shooter); unaimed neighbours jumped too; wants more attacks and no cooldowns.
- Build ok (IsAimedAt static_asserts compile); FileVersion 0.6.0.0; ship-gate PASS. Zip sha256 B3FB17B943811D066466268B4F514D59ADC34F075B066A82D94644412E315C10. Runtime: untested.

## 0.6.0 run 10 (2026-10-22, user log, ~5 min, one selected bandit 0x8E0ED, player bow + melee)
- No warnings/errors. Load log: skill at level 1 0.75 (menu-saved INI now carries it); jump OFF at load, on after the 10:32:18 menu save. Crash 10:37:37 = BetterAltTab.dll+0x25259 again (4th time), no CombatAITree frame.
- Skill: 24 Attack rolls failed at 60-79% (81 at 33-55% in run 9). **runtime-evidenced**.
- Cooldowns: no `on cooldown` skips (removed). **runtime-evidenced**.
- Jump: 2 decisions, 2 side arcs, both landed. **User-confirmed:** neighbouring NPCs no longer jump when the bow is aimed at another one (aim-line rule works).
- Stamina: one brain power attack 100% -> 17%, regen ~2%/s; stamina 0-25% for most of the fight; Retreat 229 executions, mostly at full HP with low stamina. Fixed in 0.6.1 (Backoff).
- Dodge: 19 events, all rejected.

## 0.6.1 (2026-10-22)
- Build ok; FileVersion 0.6.1.0; ship-gate PASS. Zip sha256 5297661CCC811F873270938C5CDDF4CB6A80871D1CD4C08833CC660CAF3F6744. Runtime: untested.

## 0.6.1 run 11 (2026-10-22, user log, ~4.5 min, 3 bandits, player bow + melee)
- No warnings/errors. Crash 11:20:28 = BetterAltTab.dll+0x25259 (5th), no CombatAITree frame. Jump OFF at load, on after the 11:18:42 menu save.
- Jump: 0. `Jump aim` lines: aimed stretch 11:18:44-47 had the aim line 113-166 units off at 586-1232 units, and 183-191 at ~1400 (limit 99); it grows with distance, which points to the camera offset. Fixed in 0.6.2 (crosshair actor).
- Exhaustion: Retreat now only at low health (p1.65, hp 0-25%); exhausted NPCs Backoff (463). **runtime-evidenced**.
- Stamina: power attacks and power bashes (brain and vanilla) drop stamina 25-100% -> 0 in one hit (load-order stamina costs), so bandits sit at 0-20% for most of the fight. Not changed; asked the user.

## 0.6.2 (2026-10-22)
- Build ok; FileVersion 0.6.2.0; ship-gate PASS. Zip sha256 548F0ABCE3A58BA6EDE714BCAC89C6B567DEE255B4BC467EAD0E1A69E7B24247. Runtime: untested (`autoAimActor` meaning taken from the field name and the `overAutoAimTarget` comment; first run must show `crosshair on us true` while aiming).

## 0.7.0 (2026-10-22)
- User decision: retreat/backoff on health only unless configured; power moves gated by stamina (both brain and vanilla).
- Build ok; FileVersion 0.7.0.0; ship-gate PASS. Zip sha256 5C6A85C00B4389B1E85ED46C43AF0CD594B49CAECB1CF35430B8FFDED3343476. Runtime: untested.

## Actuator audit on run 11 log (2026-10-22, 0.6.1, selected bandits only)
- Attack/power/bash intents: posted 23, taken 23 by the tree, median 37 ms (max 62); hits follow (Probe Hit). **Actuator works.**
- Movement, intent pairing (post -> `tree runs` before the next post): Advance 3/13 taken (median 5.7 s), Circle 13/22 (158 ms), Fallback 2/2, Backoff 13/30 (110 ms); 9-17 per kind superseded before ever running.
- Movement, body outcome over 0.55 s after each executed decision (moved < 25 units = still): Advancing toward 50 / still 71 / away 26 (of 149); Backoff away 17 / still 238 / toward 70 / sideways 61 (of 386); Strafe sideways 30 / still 58 (of 119); Retreat away 36/39.
- Reading: Retreat (Fallback) and attacks execute; Advance partly; **Backoff and Strafe mostly don't move the body as asked**. Likely cause (unverified): the vanilla movement nodes keep their own distance conditions (no parameter getters hooked yet), so they finish or idle when the brain's distance differs. Dodge 0% (graph rejects TKDodge). Jump runs (own physics).

## 0.7.1 (2026-10-22)
- New hooks: movement vtables slot 2 (Enter) and 3 (Exit) for Advance/Circle/Fallback/Backoff. Call convention (node, thread) and unused return checked at the Thread::Update call sites on SE 46228, AE/1.7 47483.
- Build ok; FileVersion 0.7.1.0; ship-gate PASS. Zip sha256 C4283C99D10935F0D4E50F94AC0160B942BA8C138FCBEB7EDBD6C852CE04B4A9. Runtime: untested (new hooks: watch for a crash at first combat; 0.7.0 is the same without them).

## 0.7.1 run 12 (2026-10-22, user log, ~3.5 min combat, several bandits selected in turn)
- New Enter/Exit hooks: no errors, 176 MoveNode lines. Crash 11:59:06 = BetterAltTab.dll+0x25259, 0 CombatAITree frames. **runtime-evidenced** (hooks safe in combat).
- Our branch, per node run (ran ms / moved units, medians):
  - Backoff: 28 failed at 0 ms (start dist 50-249, incl. 59 units while the target swung), 1 done (moved 0), 1 stopped. **Unusable as an actuator.**
  - Fallback: 14 done (1.4 s, moved 65, dist +65), 12 failed at 0 ms (8 of them at 250-299). Works half the time.
  - Circle: 5 done (1.4 s, moved 202), 6 stopped by brain (0.6 s, moved 46), 4 failed (2 at 200, 2 at 600+). Works when it starts.
  - Advance: 9 done (0.27 s, moved 11: already in place), 10 stopped by brain (0.95 s, moved 72, dist -32). Works.
- Vanilla's own: Advance 14 done (1.7 s, moved 152, dist -138); Backoff 13 failed (vanilla's Backoff fails too).
- A failed Backoff is re-entered every tick (12 ms) while the brain keeps asking.

## 0.8.0 (2026-10-22)
- Idle breakdown on run 12 (dist < 300, no movement > 25 units in 0.55 s and no attack): 442/899 ticks; Retreat not moving 222, no decision 66, Backoff not moving 44, Advance in place 35, failed skill roll 35.
- Start-site offsets (call order identical): SE Backoff 46724 +0x142/+0x16E/+0x1B7, Fallback 46712 +0x13B/+0x14F/+0x17B/+0x1EB; AE/1.7 Backoff 47920 +0x104/+0x130/+0x179, Fallback 47908 +0x107/+0x11B/+0x147/+0x1B0. Callees: 36756/37772 WithinHeadingAngle, 46062/47326/523984 moving-from, 49716/50643 set_two_radiuses, 49718/50645 FallbackDistance. Range = CombatInventory+0x1A0 on SE and AE.
- Build ok; FileVersion 0.8.0.0; ship-gate PASS. Zip sha256 BDDE2DC2C2D61576032B203AD48FFC09CC08E19F17710CEEC2CD929D8F30B3C3. Runtime: untested (7 new call-site hooks; 0.7.1 is the same without them).

## 0.8.1 (2026-10-22)
- Package: OAR jump-as-dodge meshes and BDI json removed (user request); OAR fallback code removed. Runs 9-12 jump lines had no '(OAR dodge animation)' suffix, so the fallback was never used.
- Build ok; FileVersion 0.8.1.0; ship-gate PASS; zip = SKSE/Plugins/CombatAITree.dll + .ini only. sha256 E2C8C0BB007B2881FFF40ED696ABF2F1F24E73B6A4DFDFAB2A368AABFA496808. Runtime: untested.

## No-decision ticks (run 12 re-check, 2026-10-22)
- 226 of 229 `Decision ... action=None` lines have no candidate list: every evaluator returned nothing. Offense (incl. Advance) and evasion require `self.isIdle`, which is `attackState == kNone` (ActorStateObserver.cpp:140), so the brain is silent while the NPC's own attack, bash or windup plays (None typically right after a vanilla `attackStart`). Not idling: the 66 'no decision' idle ticks in the 0.8.0 breakdown were counted from the previous snapshot's attack state, which was stale.

## 0.8.2 (2026-10-22)
- New Backoff trigger in EvaluateBackoff: self kFollowThrough, target in effective attack range, target not in attack recovery and knock state normal.
- Build ok; FileVersion 0.8.2.0; ship-gate PASS. Zip sha256 DDFC8C29C5CA8F054451EC2FED94234AEB781AF34B10E8370E43B2D5A2800C95. Runtime: untested (watch MoveNode `our Backoff` right after the NPC's swings; `failed` there means IsMovementAnimationDriven still holds).

## 0.8.1 run 13 (2026-10-22, user report) - **FAIL: CTD without crash log**
- User: crashed "when i approach an npc". Mod log (debug flushes every line) ends 12:17:13 at kDataLoaded, all hooks installed incl. `Backoff/Fallback start checks hooked`; no NPC selected, so no debug lines after. No crash-*.log after 11:59.
- Not proven. Suspects (all new in 0.8.0): (1) `DecisionMatrix::m_candidates`, a member vector move-assigned on every Evaluate while actor updates run on several threads: a data race, heap corruption, fail-fast with no crash log; (2) empty `topPriorityDecisions` indexed by the new random tie-break; (3) the 7 Backoff/Fallback start call-site patches.
- Rollback: 0.7.1 (run 12, runtime-evidenced) is last known good.

## 0.8.3 (2026-10-22)
- Fixes (1) with a thread_local list and (2) with an empty guard; (3) is installed only with BrainBackoffFallback=true at load.
- Build ok; FileVersion 0.8.3.0; ship-gate PASS. Zip sha256 EA4A5EAA3B2123EA118D498F82B393926517B3E1FEFEF4508C9EAAB2CA4435E1. Runtime: untested.
- Isolation plan: run 0.8.3 as shipped; if it crashes, set BrainBackoffFallback=false (patches not installed) and retry; if it still crashes, the cause is elsewhere in 0.8.x (mistake path) and 0.7.1 stays.

## 0.8.3 run 14 (2026-10-22, user log, ~3 min, bandits + a marauder, player melee)
- **No crash in combat** (user-confirmed; start patches installed). The 12:24:26 crash log is BetterAltTab.dll+0x25259 at exit, 0 CombatAITree frames. The 0.8.0/0.8.1 CTD is gone with the thread_local candidate list and the empty-tie guard (which of the two caused it is not isolated).
- Our Backoff: 52 runs, failed 5 (28/30 in run 12). 23 stopped by brain (0.6 s, moved 37, dist +43): backing works. 24 "done" after ~110 ms having moved 6, dist -7: the vanilla Backoff **Update** ends the node when 46063 (velocity toward a point) fires, i.e. the player presses in (46725+0x10B; call sequence 46725: ...32446, 46063 -> 49014 path stop -> 46229 Ascend).
- Our Circle: 36 runs, 19 failed at start (dist 50-300). Circle Enter 46720 has more gates than Backoff (IsMovementAnimationDriven, two WithinHeadingAngle incl. fCombatCircleViewConeAngle, angle vs fCombatCircleAngleMax, CheckStraightPath 32513, 32507, navmesh 45707/45708/45732); which one fails is not logged.
- Skill misses: 14 (Attack -> None 6, PowerAttack -> Feint 3, Feint -> Advancing 2, others 3).
- BackoffAfterSwing: fired once; AttackState 5 (follow-through) appeared in 1 of 968 snapshots. NPC swings go 3 (hit) -> 4 (next attack) -> 0, and the director blocks Backoff in state 4.
- Backoff decisions 235 vs Attack 17: 159 while the player swung (EvaluateBackoff target-attacking trigger, p1.65-1.95), 76 pacing waits (p1.00, slots full, inside reach).

## 0.8.4 (2026-10-22)
- Sites: Backoff Update SE 46725+0x10B / AE+1.7 47921+0x17A (callee 46063 / 47327 / 523985; delta from 46062 0xB0 / 0xB0 / 0xE0). Circle Enter SE 46720 +0x179 36487, +0x198 36756, +0x366 36756, +0x3C8 32513, +0x3E7 32507; AE/1.7 47916 +0x149 37486, +0x16A 37772, +0x2F2 37772, +0x351 33262, +0x370 33256 (dumped from vtable slot 2 / slot-4 dispatcher on all three).
- Trampoline: 15 distinct thunks, about 210 of 256 bytes.
- Build ok; FileVersion 0.8.4.0; ship-gate PASS. Zip sha256 1FA48716D8818EF39DFD2E26E17C7969065B7DEEAB6AD9D57E12B387D96A7AFA. Runtime: untested.

## 0.8.4 run 15 (2026-10-22, user report) - **FAIL: CTD** (crash-2026-10-22-21-08-50.log, 1.7.104, 30 s uptime)
- `EXCEPTION_ACCESS_VIOLATION` at SkyrimSE.exe+08F1FCD = id50648 (get_CircleAngle) +0xDD, `subss xmm2, [rbx+0x54]`, rbx = 0x25. Stack: CombatBehaviorCircle node; RCX the bandit 0x8E1A1; R15 the player.
- Root cause **proven statically**: get_CircleAngle takes (self, target) (head `mov rbx, rdx`; call site `mov rdx, r15; mov rcx, rsi`, on SE and 1.7); the CircleAngle thunk forwarded only self. A latent bug since 0.2.4, exposed when 0.8.4 added a TLS write before the call.

## 0.8.5 (2026-10-22)
- CircleAngle thunk is now (Actor*, Actor*). Argument-register audit of all hooked callees (script args.py: registers read before written): 36756 rcx/rdx (passed to 36757), xmm2; 46062/46063 rcx, rdx; 36487 rcx; 49716 rcx, rdx, r8, r9; 49718 rcx; 32513 rcx, rdx, xmm2, xmm3; 32507 rcx, rdx. All match the thunks.
- Build ok; FileVersion 0.8.5.0; ship-gate PASS. Zip sha256 142844EDC78E9BB3631F150DF02DD038110F3F274C798CEB294629BC91D25D36. Runtime: untested.

## 0.8.5 run 16 (2026-10-22, user log, ~2.5 min, 2 bandits selected in turn)
- No crash in combat; the 21:14:35 crash log is BetterAltTab.dll at exit (0 CombatAITree frames). All patches installed (Circle audit, Backoff/Fallback starts, Backoff update check, Circle angle). **0.8.4 crash fix runtime-evidenced.**
- Our Backoff: 19 runs, 2 failed at start; 16 stopped by brain at 756 ms median, moved 77, dist +37 (run 14: 24 ended at ~110 ms having moved 6); 1 done at 2.4 s, moved 160. **Backoff while pressed works.**
- After-swing Backoff: 22 Backoff decisions in AttackState 4 (next-attack window). **Fires.**
- Our Circle: 23 runs, only 2 failed at start (run 14: 19/36). The start checks on failures: angle limit 2, view cone 2, animation-driven 1; every started run passed all checks. The pre-0.8.5 Circle failures were likely the CircleAngle thunk's garbage target too (unverified).
- **New finding:** our Advance "done" 105 times at ~24 ms having moved 0, for 0x8E0ED (reach 82, brain attack range ~86) standing at 125-175 units. The vanilla Advance's CheckShouldStop (46698) calls it in place at the engine's range, which is longer than the brain's, so the NPC parks outside its own attack range.
- Skill misses 29 (Attack -> None 17, no other option).

## 0.8.6 (2026-10-22)
- Advance start radius site: SE 46703+0x16F / AE+1.7 47898+0x127 -> 49716 / 50643 (verified by CallsTarget at install). Advance Enter adds [inner] to CombatInventory+0x1A0 range for CombatTargetLocationSearch 45702 (+0x1B2) and 49062 (+0x281). CheckShouldStop(void* location, float xmm1) at 46704 / 47899 +0x121 left vanilla.
- Trampoline: 16 distinct thunks, about 224 of 256 bytes. The next new hook needs a bigger trampoline.
- INI: `BackoffAfterSwing` line repaired (glued to a comment since 0.8.2); scan for `key = value#` lines is clean.
- Build ok; FileVersion 0.8.6.0; ship-gate PASS. Zip sha256 37F49134424B70A66BC4FCEB01EB0AB2AC9B1330A18C0B1E3CEFEEB69E65934C. Runtime: untested.

## 0.8.6 run 17 (2026-10-22, user log, ~3 min, 3 bandits selected in turn)
- No crash in combat (21:26:06 = BetterAltTab at exit, 0 frames). Advance start radius hook installed.
- Our Advance: 47 instant "done" (24 ms, moved 0), 34 of them 0x8E1A4 (reach 72, range ~76, stop ~68) at 80-99 units, 9 0x8E1A1 at 80-119. 21 stopped by brain (305 ms, moved 36, 157 -> 109). The ring moved, but CheckShouldStop's tolerance still called 80-99 "in place". Fixed in 0.8.7.
- Our Backoff: 41 runs, 13 failed at start (median dist 71, close contact), 23 stopped by brain (524 ms, moved 34, 124 -> 192), 3 done (1.8 s, moved 153). Fallback: 24 runs, 13 done (1.3 s, moved 78, +88). Circle: 29 runs, 3 failed.
- In range, Backoff still outnumbers attacks: 0x8E1A4 inRange Backoff 69 vs PowerAttack 7; 0x8E1A1 Backoff 57 vs Attack 8 / PowerAttack 1; 0x8E0ED Backoff 30 vs PowerAttack 2.
- Bash: second minute posted 26, taken 2 (first minute 2/2).

## 0.8.7 (2026-10-22)
- AdvanceShouldStop thunk (void*, float) on 46704 / 47899 +0x121 -> 46698 / 47893 (args rcx + xmm1, args.py). Trampoline 512 B.
- Build ok; FileVersion 0.8.7.0; ship-gate PASS. Zip sha256 FC0B77DB91C2CBCE6DF6E287CD1F2B85C60131111A451F0619C881304F188CFA. Runtime: untested.

## 0.8.7 run 18 (2026-10-22, user log, ~4 min, 3 bandits selected in turn)
- No crash in combat (21:38:24 = BetterAltTab at exit, 0 frames). All hooks installed incl. Advance in-place check.
- Light attacks posted/taken per minute 16/16, 23/23, 11/11 (run 17: 10, 3). In range, 0x8E1A1 chose Attack 66, PowerAttack 15, Feint 46, Backoff 213 (run 17: Attack 8).
- Our Advance still ended at once 41 times (25 ms, moved 0, d0 median 129) although CheckShouldStop now refuses: Advance Enter's [inner, outer] + range band (+0x38F) keeps vanilla's outer, so the NPC was already "in band". Fixed in 0.8.8 (outer = inner).
- Our Backoff 45 runs, 7 failed; Circle 34 runs, 10 failed (start checks logged); vanilla Backoff 20 failed.

## 0.8.8 (2026-10-22)
- AdvanceRing also sets *outer = *inner for our Advance.
- Build ok; FileVersion 0.8.8.0; ship-gate PASS. Zip sha256 70CE81AC91BACD16D60FD213362228C2F4EEC9ABCF2DDA7189A6A1259AF32A23. Runtime: untested.

## 0.9.0 (2026-10-22)
- Run 18 in-range Backoff causes (0x8E1A1 etc.): target swinging p1.6-2.3 about 155, own next-attack window p1.6-1.8 about 55, pacing p1.00 47. Bash for selected NPCs: 37/38 taken within 1.5 s.
- Shortcut: the own-swing roll key is (Humanizer time - timeSinceLastAttack) in 0.25 s bins; clock drift can re-roll once mid-swing.
- Build ok; FileVersion 0.9.0.0; ship-gate PASS. Zip sha256 976446CE4736369FC5BBF42D454496431845AE36C8CB45A324AD61A11094C543. Runtime: untested.
- Run 19 (2026-10-24, 1.7.104, 5.7 min): no crash of ours (exit crash BetterAltTab+0x25259, CombatAITree frames 0). Light attacks 11/5/4/6 per min. Failed Backoff rolls: Strafe 177, Retreat 46, None 20 of 273. Our Advance done 25 (end 79-117, avg 97), stopped by brain 33. Bash never taken: 4 (0x8E0A6). Runtime-evidenced.

## 0.9.1 (2026-10-24)
- In-range ticks without an Attack candidate (selected NPC): not idle 163, target is attacking 147, last action too recent 131, LOS 8; with Attack candidate 58 (executed 26).
- Change: failed Backoff roll -> next-best only if not IsMovementAction, else None (vanilla).
- Run 20 (2026-10-24, 1.7.104, 8.8 min): no crash of ours (exit crash BetterAltTab+0x25259). Failed Backoff rolls: None 128, Bash 4. Light 6/15/5 per min. Healthy (HP>=30%) in-range ticks: Attack candidate 74 (failed skill roll -> None 54), target attacking 63, last action too recent 43, not idle 33. Retreat 443, all at HP 10-29% (0x8E0ED 430). Bash never taken 0. Runtime-evidenced.

## 0.9.2 (2026-10-24)
- Found: TargetTemporalData.attackStartTime += deltaTime (elapsed time), so per-swing keys (Humanizer defense, CombatDirector target-swing backoff) changed every tick. Now keyed on swingCount (incremented at swing start).
- Damage race from TESHitEvent (all hits, both directions), health delta between the same attacker's consecutive hits, EWMA (EwmaAlpha), cap 0.5. Shortcut: other damage between hits is credited to that attacker.
- Trade roll: stateless SplitMix64 hash of (self FormID, target swingCount) + session salt; static_assert on SplitMix64(0).
- Run 21 (2026-10-24, 1.7.104, 9 min, 0.9.2): no crash of ours. Race lines 2709 (both hits known 1856); winning 83 (0x8E1A0 our 0.101/their 0.079; follower 0xFF0008A4 0.302/0.031), losing 510 all at HP 1-13%. Trade passed 38 ticks: 29 then 'last action too recent' (own Atk 16, Bash 13), 8 attack candidate. Backoff rolls: target swing None 149, own swing None 134, Bash 7. Bash never taken 11+11 = archer 0x8E1A4 (WeaponType 8) via Feint -> ExecuteBash. Runtime-evidenced.

## 0.9.3 (2026-10-24)
- EvaluateFeinting returns for self.isRanged.
- Run 22 (2026-10-24, 1.7.104, 0.9.3, 20 min session, ~2 min combat): no warnings/errors. Light 7/11 per min, bash 8/8 and 32/32 taken, never taken 0. Race per selected NPC: 8E1A1 our 0.375/their 0.112, 8E19F 0.067/0.113, 8E0ED 0.027/0.068. Executed by race state: WIN (HP<=30) Retreat 0 / Advancing 75; LOSE Retreat 95. Backoff rolls: None 98, Bash 1. Advance runs 89 (our done 26, stopped 33). Runtime-evidenced.

## 0.9.4 (2026-10-24) data audit
- Actor::IsAttacking (1.7.104 id 38590, SE 37637) disassembled: attackState field != 0, so IsAttacking covers kHit/kNextAttack/kFollowThrough.
- Run 22: target InAtkRecov true 1/1066; NPC self non-idle states draw 32, swing 4, hit 32, next 18, bash 10.
- Run 22 misses: 17 timeouts, 15 inside reach; 5 matched a blocked=true probe hit within 1 s.
- Checked, no change: isIdle (= kNone, blocking separate), range category vs offense formula, hit/attempt matching, target reset on switch.
- Not checked at runtime: timeUntilAttackHits accuracy (60%-of-duration guess); kHit being post-impact is inferred from state order (hit -> next attack).
- Run 23 (0.9.4): target InAtkRecov true 168/1014 ticks. Executed: Advancing 312, Strafe 175, Retreat 125, Backoff 57, Feint 52, Flanking 45, Attack 17, Bash 7, PowerAttack 2.

## 0.9.5 (2026-10-25) timer speed
- Run 23: wall clock 0.11 s between evaluations of the selected NPC; TemporalSelf timers grew ~0.012 s per evaluation, equal to logged DeltaTime (one frame). Cause: Hooks.cpp passes GetSecondsSinceLastFrame per actor update; ShouldProcessActor throttles to processingInterval, but GatherState got the frame delta.
- Run 23: "last action too recent" blocked 185 in-reach offense evaluations; on target-recovery ticks execution was Strafe 133, Attack 4.
- Fix: ShouldProcessActor returns the accumulated throttle time; GatherState uses it. Untested in game.
- Run 24 (0.9.5): DeltaTime 0.109 s per evaluation vs 0.11 s wall clock. Executed: Advancing 290, Retreat 139 (128 by one NPC at <10% HP), Strafe 100, Flanking 96, Backoff 92, Attack 43, Bash 25, Feint 14, PowerAttack 1. "Last action too recent" 49 (run 23: 185). Target recovery ticks: Strafe 37, Retreat 11, None 10, Attack 9.

## 0.9.6 (2026-10-25) bash anti-spam
- Run 24: 0x8E1A1 posted Bash at 20:03:54.399/.509/.619 with own AttackState=1 each tick, tree took each; one probe hit (blocked=true) at .813; LastBash reset to 0.11 s each time. Bursts of 3-5 at 49.6, 54.4, 55.2, 00.2.
- Fix: EvaluateInterrupt returns None while timeSinceLastAction < 0.3 s. Untested in game.
- Run 25 (0.9.6): Executed Retreat 396, Advancing 337, Flanking 122, Strafe 110, Attack 37, Backoff 34, Feint 16, Bash 12, PowerAttack 1. "Last action too recent" 46. LastAction resets: Atk 29, Bash 17 (no consecutive-tick bash bursts).

## 0.9.7 (2026-10-25) cornered retreat
- Run 25: 0x8E0ED at 24% HP executed Retreat 389 times from 20:12:06 to 20:13:06; distance up to 1820; player health 92% to 100%; NPC health stayed 24% until it dropped to 4%.
- Fix: per-actor retreatTime accumulates while the last executed action is Retreat, reset when health rises 5% above its lowest; health and race retreats are skipped at >= 8 s. Untested in game.

## 0.9.8 (2026-10-25) animation event probe
- Run 25 actuator audit: intents posted/taken light 58/58 (attack data 52), power 5/5 (5), bash 29/26. Taken attack with a later TotalAtks sample: swing started within 1 s 14/33; bash 6/25, 0/4 at stamina 0. Example 20:10:27.177: attack taken, attackStart chance 1.00, IdleDur 0.87 s at 27.494 (idle through the hand-off). Movement: changes advance 85, circle 96, backoff 27, fallback 8; preemption stops ours 137, vanilla 59.
- Hook target from the CommonLib headers the build uses: TESObjectREFR bases TESForm 00, BSHandleRefObject 20, BSTEventSink<BSAnimationGraphEvent> 30, IAnimationGraphManagerHolder 38, so VTABLE_Character[3]; NotifyAnimationGraph is its slot 01. Built; untested in game. If no AnimEvent lines appear while NPCs swing, the attack node does not go through this vfunc.
- Run 26 (0.9.8): AnimEvent attackStart accepted 41, attackPowerStartForward 1, bashStart 2, none rejected. Taken attack intents: event sent 45 (swing 33, no swing 4, no sample 7); picked attack (shaper ran) but no event 12; no shaper and no event 8. Bash: no event 9 of 13. TK Dodge 14/14 accepted, displacement 150-340 units in 0.7 s (4 short, 60-110). Cornered rule not exercised (max Retreated 3.6 s).

## 0.9.9 (2026-10-25) StartAttack check probe
- Disassembly (se.py, SE 1.5.97, AE 1.6.1170, AE 1.7.104): StartAttack calls hit cone with rcx=actor (also CombatAnimation ctor's actor), rdx=target, r8/r9 = actor+0x54 / target+0x54, stack: attack data, float, r15, byte 0; then `test al,al / je` skip. CheckAttack(rcx, rdx=candidate entry), same skip. Offsets: SE 0x493 / 0x4A6, AE 0x3F1 / 0x404 in both 1.6.1170 and 1.7.104. Before them per candidate: a time check ([global] - entry+0x28 > entry+0x2C, likely a per-attack cooldown) and the stamina cost gate (SE 25863 / AE 26429), not probed.
- Build ok; untested in game.
- Run 27 (0.9.9): hooks installed, no warnings. Checks within 0.15 s of a taken intent: attack 53 = fired 31, no check 11 (speed 424-659 on 4: sprinting), cone pass > range FAIL 8 (56-99 units, all within ~1.3x reach; passes seen at 42-122), cone FAIL 3 (+57/+58/+100 deg at 52-66 units). Bash 15 = fired 3, no check 12 (stamina 9-41). 21 of 65 cone checks were vanilla's own attempts while the brain advanced (range FAIL at 170-296 units). Attacks fire at stamina 0, so stamina is not the attack gate.
