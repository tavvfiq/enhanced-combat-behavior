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

## 0.10.0 (2026-10-25) swing-start timer, sprint and facing gates
- Change from run 27 evidence (see 0.9.9 entry). The timer reset moved into the NotifyAnimationGraph hook (VTABLE_Character[3] slot 1, runtime-proven in runs 26-27), which runs on the tree thread; ActorStateObserver's map is lock-guarded (Modify, no insert).
- Expect next run: "last action too recent" lines no longer follow a taken intent that sent no AnimEvent; Offense skips `sprinting` and `not facing the target`; fired/taken attack ratio above 31/53. Untested in game.
- Run 28 (0.10.0): no warnings. Taken attack intents (selected NPCs) 68: fired 37 (5 after a range FAIL retried in the same tree visit), no check 16 (all 0x8E1A4, sprint flag false, 6 within 0.7 s of a bow-to-dagger switch), other check fails 15. Hit cone FAIL also at 5-36 deg, so the cone is not only facing. Offense skips: target is attacking 22, not facing 33 (18 with dot < 0), last action too recent 7, sprinting 0. Bash taken 12: fired 1, no check 11 (stamina 0-8% on 10). Cornered rule: 0x8E0ED at 23% hit Retreated=8.1 s at 21:01:16.8, then Advancing 54, Strafe 18, Attack 6, 4 hits landed, down to 9% at 21:01:34.5.

## 0.11.0 (2026-10-25) dodge targeting, bash stamina, weapon switch
- Run 28 dodge audit (projectile line matched to the same tick): 9 executed Dodge: 7 projectile sidesteps, all shooter 0x8FFA4 (bandit, ally of the dodgers; 17 sidestep lines, all from it); 2 evasion dodges at 254 and 340 units, target attacking, orientDot 0.80 / 1.00, no power attack. 1 ForwardDodge (gap-close, target facing away, dot -0.30).
- Evasion code before the change: dodge when target orientationDot > 0.7 and (attacking at 250-400 units, or blocking at 250-375); no aim or reach check.
- GMST name fStaminaBashBase taken from the disassembly annotation of the stamina gate 25863 (COMBAT_AI_RE.md); its value is read at runtime and logged.
- Expect: no `Projectile ... from <ally>` sidestep lines; `Bash stamina cost` line at the first bash evaluation; Offense skips `weapon just switched`. Untested in game.
- Run 29 (0.11.0): no warnings. `Bash stamina cost 35.0 (fStaminaBashBase)`. Executed Dodge 0, ForwardDodge 1 (351 units, target facing away); Projectile lines 0. Taken attack intents fired: 0x8E1A1 21/36, 0x8E0ED 16/23, 0x8E1A4 7/15; no-check cases had lastAtk 0-3 s on 12 of 15. Bash fired 5/6. Offense skips: weapon just switched 3, not facing 19, priority too low 22 (all 0x8E1A4 at 0.34). 0x8E1A4 with dagger (WeaponType 3): Feint 75, Attack 13; bash posted 11, taken 0 in its minute.

## 0.11.1 (2026-10-25) defend refusals
- Fix from run 29 above. Untested in game.

## 0.12.0 (2026-10-25) close dodge
- Before: EvaluateEvasion dodged only at EvasionMinDistance 250 to SprintAttackMaxDistance 400; nothing below 250. Back-off priorities in run 29: up to 2.15 (most common), CombatStyleEnhancer adds up to +0.15 to dodge/backoff; won close dodge set to >= 3.0.
- New config DecisionMatrix.CloseDodgeChance 0.3 (INI, menu, load log). Untested in game.

## 0.13.0 (2026-10-25) dodge direction and path
- Code audit: StateHelpers::GetActorForwardVector = (sin h, cos h) (heading clockwise from north); DetermineDodgeDirection used atan2 (counter-clockwise) and mapped +45..+135 deg to eventRight. Facing north (0,1), dodge east (1,0): diff -90 deg -> eventLeft. Swapped. Not runtime-confirmed (run 29 had no side dodges; older logs overwritten). Evasion side was a fixed +90 rotation of the attacker's facing; only obstacle-free check was the ledge probe (ray down, 180 units out, 200 drop).
- Run 26 had 4 short dodges (60-110 units vs 150-340), consistent with bumping obstacles.
- Expect next run: dodgecheck.py reports event == movement side for accepted dodges; `blocked (obstacle|actor|ledge)` entries when near walls. Untested in game.
- Run 30 (0.13.0): no warnings. dodgecheck.py: accepted dodges 5, event == movement 5 (Right 174/159 units, Back 81, Forward 208/214). `no clear direction` 17, all `TKDodgeForward blocked (actor)` (forward dodge, path ray hit the target). Close swing lines: dodge 13, no dodge (roll) 59 (per tick, not per swing). Of the "dodge" ticks: candidate (8, 3.00) lost to PowerAttack 3.05 / Attack 3.85 and 4.70; some had no Dodge candidate (stamina check after the log line).

## 0.13.1 (2026-10-25)
- Fixes from run 30 above. Untested in game.

## 0.13.2 (2026-10-25) shoulder rays
- Path check: 3 kLOS rays (centre, +-22 units perpendicular), 35-220 units out at +60 height; up to 3 rays per direction, 9 per attack dodge with all fallbacks. Wall blocking not yet seen in a log. Untested in game.

## 0.14.0 (2026-10-25) aimed-at-us gates
- Same TargetAimedAtUs (IsAimedAt with body radius + 75, or the player's crosshair actor) as the 0.11.0 dodge. Close up it passes for any forward facing (at 100 units the aim limit ~100 covers dot > 0); at 300 units it needs about 20 degrees. Untested in game.
- #4 finding from the RE notes (ActionEquipmentSelector case table, marked confirmed): Block Link only under case 3 Shield; Melee Link (attack tree) under case 1 Melee. Consistent with run 29: 0x8E1A4 (dagger) 11 bash posts, 0 taken; archers 0 of 23 in run 21.
- Run 31 (0.13.2): no warnings. dodgecheck.py: 12 accepted dodges, 10 match (Right 5, Left 2, Forward 2, Back 1); mismatches at 35.171 (Forward event, measured Left 198) and 49.554 (Right, measured Back 132), both followed by advancing in the 0.4-0.8 s window. Blocks: `TKDodgeLeft blocked (obstacle)` -> Right 189 units; `TKDodgeForward blocked (actor at shoulder)` -> Back 139. Close swing ticks: dodge 20, no dodge (roll) 69, no dodge (stamina) 21; Executed Dodge 7. Executed ForwardDodge 47 vs 4 TKDodgeForward events: 0.13.1's executor fallback reported advances as forward dodges (CombatDirector.cpp already relabels a failed ForwardDodge as Advancing).

## 0.14.1 (2026-10-25)
- Executor ForwardDodge fallback removed. Untested in game.

## 0.15.0 (2026-10-25) parry and timed block without mods
- Code audit before the change: EvaluateParry had no mod check (EldenParry only feeds ParryFeedbackTracker via a mod callback; parry scoring does not read it). EvaluateTimedBlock returned while SimpleTimedBlock.esp was missing and ExecuteTimedBlock failed without its window spell. Config.h defaults were already true; the shipped INI set both false.
- Timing is the open question: both use timeUntilAttackHits (hit assumed at 60% of the estimated swing), unverified. parrytime.py measures executed Parry/TimedBlock against the player's next Probe Hit on that NPC. Untested in game.
- Run 32 (0.15.0): `Simple Timed Block not found: timed blocks run as well-timed plain blocks`. Executed TimedBlock 2, Parry 0. parrytime.py: 12.547 TimedBlock -> blockStart +0.06, blockHitStart +0.29, player hit 0.29 s later blocked=true; 53.447 TimedBlock -> no event, no hit. TimedBlock candidate on ~35 ticks at 1.36-1.75, beaten by Backoff 1.65-2.15, Strafe 1.45-1.80, Attack 2.35, close Dodge 10.2; skill-roll fails 3. Swing ticks in 50-200 units 150; TimeUntilAtkHits in 0.05-0.33 on 65. hittime.py: 41 player hits, swing start (as the observer saw it) to hit median 0.46 s (0.17-1.21), estimate 0.22 s (EstAtkDur 0.37 for every swing); error mostly +0.16 to +0.40.

## 0.16.0 (2026-10-25) learned hit timing, defend roll
- Fixes from run 32 above. The learned sample uses the same reference as attackStartTime (the observer's swing detection, up to one evaluation late), so prediction and sample agree. Untested in game.
- Run 33 (0.16.0): load line `defend 0.50`. Executed TimedBlock 9, Parry 0. parrytime.py: 4 with block events, player hit blocked 0.23-0.30 s later (blockStart/blockAnticipateStart/blockHitStart); 5 without an event: 3 from 0x8E1A4 (dagger, no shield; block intents 6 posted, 2 taken in its minute), 0x8E0ED at 55.713 (0.33 s after its own block at 55.384), 0x8E1A1 at 38.990. Defend ticks: timed block won 14, lost 19; parry window 4 (all lost), parry needs >= 35 stamina. Defend skill-roll fails 5. Player hits: blocked 5, not blocked 24. HitTiming samples 0.12-1.30 s, average settling 0.47-0.53 s.

## 0.16.1 (2026-10-25)
- Fixes from run 33 above. Untested in game.

## 0.16.2 (2026-10-25) vanilla weapon-block rule
- Disassembly, SE 1.5.97: CombatInventoryItemOneHandedBlock vtable 0x141681cf8, GetType slot 9 = 43824 = CombatInventoryItemShield's (TYPE kShield, CommonLib kOneHandedBlock = kShield). Created in CombatInventory 43626 (+0x2d7: `call 24852` on Actor+0x1F0 race; 24852 = `test byte [rcx+0x428], 0x7e; seta al` = TESRace::validEquipTypes bits 1-6), ctor 43833 at +0x337. CheckShouldEquip 43837: other item's vfunc slot 9 (GetType) == 1 (Melee), then `movzx ecx, byte [weapon+0x19d]` (weaponData.animationType) `and cl, 0xfd`, true when non-zero (not 0 hand-to-hand, not 2 dagger). AE not disassembled (same data-driven rule assumed). Untested in game.

## 0.17.0 (2026-10-25) dagger block
- Disassembly: SE 43837 `and cl, 0xfd` at +0x72 (bytes 80e1fd); AE 1.7.104 and 1.6.1170 id 45056 `test byte ptr [rax+0x19d], 0xfd` at +0x6b (bytes f6809d010000fd). Same control flow in all three (controller check, right-hand combat item GetType == 1, then the mask). Install checks the byte is 0xFD, writes 0xFF.
- Unknown until tested: whether the humanoid behavior graph accepts blockStart / bashStart with a dagger drawn (AnimEvent lines will show accepted/rejected). Untested in game.

## 0.17.1 (2026-10-25) dual-wield block probe
- Source checked: WastelPlaster/SimpleDualWieldBlock src/main.cpp (GitHub, main): key down sets `actorState->actorState2.wantBlocking = 1` and NotifyAnimationGraph("blockStart"); key up clears it and sends "blockStop". No behavior files. CommonLib ActorState2::wantBlocking (bit 8 of ActorState+0x0C).
- Probe built; untested in game. Pass: `blockStart accepted` and `blocking=true` after 0.2 s for a dual-wield or spell+weapon NPC.
- Run 34 (0.17.1): load `Dagger block: NPCs with a dagger can block and bash`. DualBlock probe 52 tries, blockStart accepted 52: spell+spell 43 and empty+spell 4 never blocking (0 of 26 hits blocked); spell+dagger (spawned 0xFF000875) 5 tries: blocking=true 2 with the player's hit blocked=true (blockHitStart / blockAnticipateStart), 3 not (own attackState 3 at start; brain Executed Attack 0.1 s in; one unexplained). No dual-weapon NPC fought.

## 0.18.0 (2026-10-25) DualBlock tree node
- RE: Magic builder SE 48065 calls initNodes (46263) at +0x4AC; AE 1.6.1170 and 1.7.104 id 49112 calls 47518 at +0x7AC. Idle vtable (SE 256701 / AE 205318): RTTI at [-1], 10 slots; slot 2 Enter, 3 Exit. The Magic tree shape (Context > Repeat > child) is checked at runtime; a mismatch logs an error and leaves it vanilla.
- Built; untested in game. Pass: `Tree: Magic tree extended`, DualBlock blockStart/release lines for a weapon+spell or dual-wield NPC, the player's hits blocked=true during holds, no stuck blocking after combat.
- Run 35 (0.18.0): `Tree: Magic SetRoot hooked`, then `Tree: Magic tree has an unexpected shape` (root's child is not a Repeat). Dual block posts 3, all for 0xFF0008C8 (Bandit Chief): blockStart accepted 3; 00:04:50.234 player hit blocked=true (blockHitStart); release blockStop accepted 2, rejected 1 (00:04:50.590, after a blockStop at 50.418 from the executor's pre-movement block release when Feint executed). SE 48065 call list: Repeat factory 32409 at +0x1AE and +0x31D, NextSelector 32407 at +0x191 and +0x302 (one Repeat per cast branch).

## 0.18.1 (2026-10-26)
- Fixes from run 35 above. Untested in game.

## 0.18.2 (2026-10-26) cast stops for the block
- Disassembly: NodeObject1<CastImmediateSpell,bool> vtable SE id 266704 / AE 213718 (Exit 48104 / 49141, Update 48127 / 49159); <CastConcentrationSpell,bool> SE 266706 / AE 213733 (Exit 48103 / 49140, Update 48126 / 49158). Both Exits call SE 48040 / AE 49087, which calls SE 33630 / AE 34408 = MagicCaster::InterruptCast (CommonLib RELOCATION_ID(33630, 34408)). Same in AE 1.6.1170 and 1.7.104. Untested in game.
- Run 36 (0.18.2): `Tree: Magic tree has no Repeat node`. Dual block 0xFF000826 (Bandit Chief, right WeaponType 5... logged melee 1H) 5 started, player hits blocked=true 3 (blockHitStart). One early `blockStop accepted` 37 ms before our release after the hold ended (executor pre-movement release once WantsDualBlock was false).

## 0.18.3 (2026-10-26)
- Tree dump only. Untested in game.
- Run 37 (0.18.3) dump, AE 1.7.104 vtable offsets -> RTTI (se.py v17): 'Magic Context' +0x1961B70 (213692 CreateContextNode1<ContextMagic>) > 'ValueNode - Casting Type Selector' +0x1961E88 (213737 ValueNodeT<CastingType expression>, 2 children) > 'ValueNode - Cast Immediate Repeat' / 'Cast Concentration Repeat' +0x1961EE8 (213739 ValueNodeT<CastingType>, 1 child) > 'Cast ... Spell Selector' +0x18ED320 (205291 FallbackSelector<NextChildSelector>, 3 children: RandomNode Prepare Dual Cast (213741 > 213705 PrepareDualCast > 213718 / 213733 NodeObject1<Cast..., bool> = dual cast), RandomNode Cast (> 213720 / 213735 NodeObject<Cast...> = single cast), Idle 205318). No vtRepeat in the tree.

## 0.18.4 (2026-10-26)
- SE equivalents by RTTI name: ValueNodeT<CastingType> 266709, NodeObject<CastImmediateSpell> 266705, NodeObject<CastConcentrationSpell> 266707 (Exit 48104/48103 -> 48040 -> 33630, same as the dual-cast vtables 266704/266706). AE 1.6.1170 same ids as 1.7.104. Untested in game.
- Run 38 (0.18.4): `Tree: Magic tree extended (dual block in 2 casting types)`, no warnings. 0xFF00082B (Bandit Chief, sword + spell): dual block posts 7, blockStart accepted 7, `immediate cast stopped for the block` 5 (before blockStart on 4, same ms after on 1). Player hits landing during a hold: blocked=true 2 (blockHitStart), blocked=false 1 (00:..:20.748, block already gone: blockStop rejected at release). Releases: blockStop accepted 4, rejected 3. All 57 player hits on it: blocked 2; NPC own attackState 1-4 on 29; no Defend line since the last hit on 20; defend roll lost 4; won but not blocked 2. Defend window ticks: won 7, lost 18.

## 0.18.5 (2026-10-26) the 20 unchecked hits
- Run 38, 0xFF00082B, 20 player hits with the NPC idle and no Defend line since the previous hit (miss20b.py): in the window but dist 42-47 < TimedBlockMinDistance 50 on 8; predicted hit never in 0.05-0.33 (tuh 0.45-0.83 while the hit came 0.01-0.56 s later) on 6; in window with no gate visible in the log on 6; own attack 3; facing away 1; no tick saw the swing 1 (some hits have two reasons). HitTiming for it: samples 0.59, 0.24, 0.98, 1.23, 1.23 then 17 samples 0.12-0.45 "ignored", average 0.77-0.94. It posted defend (bash) 8, taken 0; summary `block/bash never taken: 5 (most 0xFF00082B x5)`, so DefendUnavailable was on (3 in a row, 10 s) and EvaluateInterrupt returned before the timed block. Target PAtk false on all 502 attacking ticks; all 57 hits power=false.
- Fixes built; untested in game.
- Run 39 (0.18.5): 0xFF000815 (Bandit Chief, sword + spell). Player hits 57: blocked 7 (run 38: 2). Defend ticks: won 21, lost 34 (run 38: 7 / 18). Executed TimedBlock 17 (dual block): blockStart then hit blocked=true 6 (hit 0.05-0.52 s after the request); blockStart then hit blocked=false 7, hit 0.56-0.78 s after the request (hold = predicted hit + 0.25 s ended first); no hit 2; no event 1. HitTiming samples 57, bimodal: 33 at 0.12-0.50 s, 24 at 0.63-1.11 s; median ended 0.70-0.82 s.

## 0.18.6 (2026-10-26) dual block holds through the swing
- Fix from run 39 above. Untested in game.
- Run 40 (0.18.6): 0xFF000819 (Bandit Chief). Player hits 73: blocked 5. DualBlock blockStart 30, `swing over (hit landed)` 30, `(target stopped attacking)` 1; parrytime.py: blockStop +0.15 s after blockStart typical, then the hit unblocked. Cause: isInAttackRecovery (kHit/kNextAttack/kFollowThrough) is already true between combo swings.

## 0.18.7 (2026-10-26)
- Fix from run 40 above. Untested in game.
- Run 41 (0.18.7): 0xFF00084A (Bandit Chief). Player hits 53, blocked 2. Executed TimedBlock 6: blockStart 4 (cast stopped first 3), `the blow landed, ending the hold` 5, releases accepted 4; 59.534 block up 0.57 s, the blow blocked=true. Of the unblocked hits: NPC own attackState 1-4 on 37, defend roll lost 8, no defend check 5 (predicted hit never in window 4, no tick saw the swing 1). Defend window ticks won 6, lost 29. HitTiming median 0.42-0.54 s.

## 0.19.0 (2026-10-26) decision matrix + pacing audit
- Read: DecisionMatrix.cpp (Evaluate, Interrupt, Parry/TimedBlock, Evasion, Survival, Offense, Backoff, Flanking, Feinting), CombatDirector::ApplyCombatPacing, AttackCoordinator, CombatStyleEnhancer (assignments).
- Fixed: see CHANGELOG 0.19.0. Evidence is the code; none of these were measured in a run. Flanking: closestAllyPosition is a world position (Combat log line "ClosestPos=(-70282.1,-67908.2,...)"), so allyDistSq ~ 9e9 > 1e7 and EvaluateFlanking returns early in exteriors.
- Build ok; untested in game.

## 0.19.1 (2026-10-26)
- Audit choices A/B/C applied as described in CHANGELOG 0.19.1. Untested in game. Expect: Flanking chosen by EvaluateFlanking (not only the pacing hold) in exterior group fights; far fewer Feint executions against a non-blocking player.

## 0.20.0 (2026-10-26) counter attack
- Code: isGuardCounterActive drove only EldenCounter's Val_GuardCounter; EvaluateOffense gives +2.0 and prefers a power attack while it is set (bonus now kept, 0.19.0). New: kHitBlocked melee hit on a non-player opens 0.6 s. Untested in game. Pass: `Counter 0x..` line, then an Attack/PowerAttack executed within 0.6 s and an attackStart*/attackPowerStart* AnimEvent.
- Run 42 (0.20.0, group of 3, outdoors): Executed Flanking 1662, Backoff 181, Attack 46, Advancing 44, TimedBlock 17, Dodge 5, Strafe 5, PowerAttack 2, Feint 2 (B works: was 75-79), ForwardDodge 2. In range: Flanking 292, Backoff 174, Attack 46. Flanking at 1.65 from EvaluateFlanking (the pacing hold is 1.0). Counter windows 9: followed by Attack 5.33 / 6.68 / 2.20-2.65 with attackStart on 4; lost to Flanking or "target is attacking" on the rest. Pacing: slot granted 23, waiting 30, retaliating 11.

## 0.20.1 (2026-10-26)
- Fixes from run 42 above. Untested in game.
- Run 43 (0.20.1): Executed Flanking 593, Advancing 243, Strafe 161, Backoff 114, Retreat 72, Attack 32, TimedBlock 8, ForwardDodge 5, Dodge 4. In-reach ticks (inrange5.py): held with an attack candidate -> Backoff 60; free, no attack candidate 157 (Flanking 56, Strafe 47, None 46, Backoff 8); attack candidate lost to Flanking 19 / Backoff 18; Attack 30. Offense skips: not facing 38, target attacking 26, last action too recent 14. Attack candidate scores 0.55-4.78 (median 2.62). Counter windows 11, swung 1; attempts show blockStop, attack posted and taken, then `hit cone FAIL (-26 deg)` / `range/path check FAIL` (99 units). Pacing: granted 17, waiting 25, retaliating 6.

## 0.20.2 (2026-10-26)
- Fixes and tuning from run 43 above. Untested in game.
- Run 44 (0.20.2, group of 4 selected in turn): Executed Flanking 424, Advancing 248, Strafe 43, Backoff 35, Attack 30, TimedBlock 6. In-reach ticks 309: no attack candidate and no Offense skip 225, of which own attackState 1-6 on 218 (already swinging); Offense skips: target is attacking 17, not facing 8 (run 43: 26 / 38), last action too recent 0 (run 43: 14). Attack candidate scores 1.85-5.91, median 3.55 (run 43: 2.62). Pacing granted 11, waiting 9, retaliating 3. Selected-NPC swings 26, hits on the player 25 (run 43: 19 / 17). Counter windows 8: two led into an attack state at once (vanilla block-attack), one lost 5 ticks to a failed skill roll (offense rolls hold 1 s), then attacked at 3.11.

## 0.21.0 (2026-10-26) ranged positioning
- RE: Movement selector option 8 RangedMovement (CombatInventory::QRawMaxRange 48463 > 1024) per COMBAT_AI_RE.md; builder found by vtables CreateContextNode<ContextRangedMovement> (SE 266932 / AE 214000) and NodeObject<MaintainOptimalRange> (SE 266933 / AE 214013): AE 1.6.1170 and 1.7.104 id 49468 calls initNodes 47518 at +0x957; SE factories 48480/48484/48485 are called from 48476, which calls initNodes 46263 at +0x495. Shape checked at runtime (Context > Repeat > child).
- WEAPON_TYPE: CommonLib TESObjectWEAP.h kStaff = 8, kCrossbow = 9; IdentifyWeaponType mapped 8 -> Crossbow (IsCrossbow() runs first, so real crossbows were right; staffs were wrong).
- Untested in game. Pass: `Tree: RangedMovement tree extended`, Ranged lines for an archer/mage, MoveNode lines for its Fallback/Advance.
- Run 45 (0.21.0): `Tree: RangedMovement tree extended`. Archers 0x8E1A4, 0x8FF14: Ranged "fall back" 216, "close in" 57 (2011-2168 units or out of sight); with the bow (WeaponType 8) Executed Retreat 92 / 59, Advancing 54; with the dagger (3) the melee rules (Flanking 132 / 91, Bash 8 / 12). Our Fallback in RangedMovement: failed at start 23 (moved 0, 42-391 units), ran 6 (moved 8-54, target closing). Circle failures there: path / straight-path checks. AnimEvent bowAttackStart 25, attackRelease 18. "Bandit Mage" 0xFF0008F4 had a dagger in the right hand (melee rules): no caster tested.

## 0.21.1 (2026-10-26)
- Contact cutoff from run 45 above. Untested in game.
- Run 46 (0.21.1): 0x8E19E Bandit Sorcerer: with the spell (WeaponType 11) Retreat 71, Strafe 40, Backoff 7; our Fallback ran 4 (moved 48-81); later dagger (3): melee rules. 0x8E1A4 archer (bow throughout): Retreat 54, Strafe 40, Backoff 16; our Fallback failed 9 of 9 (moved 0), own attackState at the failure 8/9/12 on 7, 0 on 2. Ranged "left to vanilla" (contact) 198, "fall back" 136. bowAttackStart 10, attackRelease 8. Player hits: sorcerer 15, archer 12.

## 0.21.2 (2026-10-26)
- No fall-back request mid-draw/cast. Untested in game.

## 0.22.0 (2026-10-26) archer shot timing, line of fire
- RE: SE Ranged builder 32398 (refs CreateContextNode1<ContextRanged> 256696, NodeObject<RangedAttack> 256699): `lea rax, [rip+0x34b5c6]` (48 8D 05) at +0xA3 = 49772, next to the "Ranged Attack" name, SetRoot at +0x1BC. AE 1.7.104 and 1.6.1170 builder 33149: `lea rax` (48 8D 05) at +0x111 = 50700 (same prologue as SE 49772: push rbx / movaps / mov rbx, rcx / call), SetRoot at +0x3C1. Install checks the 48 8D 05 bytes; the stub is a 14-byte jmp [rip] in the SKSE trampoline. The function is called with the actor in rcx; the thunk forwards rcx and rdx unchanged.
- Untested in game. Pass: load line `Tree: Ranged attack chance redirected`, `shot chance x0.3` while the player blocks facing the archer and fewer bowAttackStart then, x2 lines on openings.
- Run 47 (0.22.0): `Tree: Ranged attack chance redirected`, no warnings. 0x8E1A4: bowAttackStart 31 / attackRelease 31 (run 46: 10 / 8); shot factor x2 225, x0.3 8, x0 8; Ranged: finishing its shot first 68, contact 90, close in 37, fall back 5. 0x8FFA4: draws 12, x2 41, x0.3 33, close in 35. No "step aside" (x0 ticks were inside earlier branches: contact / close in / mid-draw).

## 0.22.1 (2026-10-26)
- Opening without "looking away". Untested in game.

## 0.22.2 (2026-10-26) line of fire, Fallback audit, cooldown probe
- RE, SE 46712 (Fallback start) fail jumps to +0x2CA: WithinHeadingAngle +0x13B (je), target-leaving 46062 +0x14F (jne), IsMovementAnimationDriven 36487 +0x15F (jne), WithinHeadingAngle +0x17B (je), comiss xmm3,xmm1 +0x1FE (jbe, after FallbackDistance 49718 at +0x1EB), CheckStraightPath 32513 +0x27B (je). AE 1.6.1170 / 1.7.104 47908: 37486 at +0x12B, 33262 at +0x239 (same call order). Facing/leaving pass for ours since 0.8.0.
- StartAttack per candidate (SE 48139): cooldown compare at +0x279..+0x28A (no call), stamina cost call 25863 at +0x29B (`comiss xmm0, xmm7; ja skip`), weight 49748 at +0x2AE; AE 49170: 26429 at +0x27A, 50675 at +0x28D.
- Untested in game.

## 0.23.0 (2026-10-26) cast hold, SCAR data reader
- Cast hold: same splice point as the 0.18.4 dual block (ValueNodeT<CastingType> cases). No new engine sites.
- SCAR reader ported from source read by the 0.20.2 research (DataHandler.cpp:37-118, SCARActionData.cpp:9-54, Function.cpp:56-111 at e62b63a). Differences: no TrueHUD overlay, no Precision reach (reach = our weapon reach unless WeaponLength), no AABB height overlap check, Chance clamped to 0-100 (SCAR clamps to -180..180), CoolDownAlias/ConditionsAlias parsed but ignored.
- Untested in game. Pass: `SCAR data reader self-test passed`, `SCAR 0x..` lines for a SCAR-patched NPC, no crash while it is selected.
- Run 48 (0.23.0, 4 min, camp bandits: 2 archers, sorcerer, scout, mage): loads, no warnings, no crash with SCAR NPCs selected. `SCAR data reader self-test passed`; 18 `SCAR 0x..` lines (0x8E1A1, 0x8E19E): 2 actions per clip (ADXP_NPCPowerAttack 30-60%, ADXP_NPCNormalAttack 100%), distance fit changes with range. Sorcerer 0x8E19E: 4 concentration stops, 144 step-asides, held fire 28.8 s of 81 s (11 holds, longest 15.3 s: step-aside Circle refused at start, `path 0` / `view cone 0`, then Advancing, which skips the step-aside); flames still cast (40 PFLA threats). Archer 0x8FF14: 25 step-asides, holds of 3.4 s at most. Stamina probe: power/bash candidates refused at 0 stamina (scout drained at 06:29:58), light attack cost 0. Fallback audit: ours refused by the straight-path check 6x (mage 330-370 from target); runs 92-206 units when it passes.

## 0.23.1 (2026-10-26) capped hold, step-aside other side
- No new engine sites. Untested in game. Pass: no hold over ~3.1 s in a row (`shot chance x0.0` runs), `held 3 s, shooting anyway` and `other side: the last one failed` lines, the sorcerer steps aside rather than "out of sight -> close in" with an ally in line.
- Run 49 (0.23.1, 4 min, camp bandits incl. archer 0x8E1A4 and Conqueror Sorcerer 0xFF0008FD): loads, no warnings. Hold cap works: archer 12 holds, five exactly 3.0 s, 30 `held 3 s, shooting anyway`; sorcerer 3 holds, longest 2.1 s. Side flip works (40 `other side` lines, Circle side alternates), but the archer's Circle failed on both sides at the start's path check (`path 0` 32x, 32507/33256), so it rarely moved out of the line. 23 bow draws: 15 free, 5 right after a capped hold, 3 during a hold (two 1.1 s after a release: vanilla's chained shot, not re-rolled through the chance).
