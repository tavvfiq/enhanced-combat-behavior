# STATE - CombatAITree

**Goal:** EnhancedCombatAI's brain (DecisionMatrix, skill rolls, cooldowns) driving vanilla leaf
nodes through our own behavior trees, instead of raw animation events. Design: `TREE_DESIGN.md`.

**Owner folder:** D:\Modding\combat-ai-tree (git). Research and tools: D:\Modding\skyrim_re.

**Active version:** 0.31.1 (CURRENT.txt). Releases: `releases\<version>\CombatAITree-<version>.zip`, one per
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
- 0.5.1, run 7 (~3 min, 2 bandits, player melee): no errors. Menu works (config saved from it). Preemption
  looped every frame for one bandit (fixed 0.5.2). Short-weapon bandit idled at 60-100 units (reach 48 ->
  range 52; fixed 0.5.2). All 117 TKDodge events rejected by the NPC graph. Jump never eligible (player had no bow).
- 0.5.2, run 8 (~5 min, 3 bandits, player bow + melee): clean. Preemption no longer loops (min 316 ms between
  stops); short-weapon reach fixed (bandit attacks in range). Jump: 0 (EnableJumpEvasion most likely off, shipped
  default). Dodges all rejected again. "Target is attacking" gate blocked 92 attacks while the player drew a bow.
- 0.5.3, run 9 (~3.5 min, player mostly bow): no errors. Jump works: 5 side arcs, 5 landed, no aborts. Bow gate fixed
  (0 ranged "target is attacking" skips). The crash at 09:57:56 is BetterAltTab.dll (Xaudio stop in its WindowProc),
  the same as 09:21 and 09:50.
- 0.6.0, run 10: skill 0.75 and no cooldowns confirmed; 2 jumps, both landed; user confirmed neighbours no longer jump. Power attack emptied stamina
  (100% -> 17%), then low-stamina Retreat ran a full-health bandit off for ~25 s.
- 0.6.1, run 11: exhausted NPCs back off (Retreat only at low HP). No jump: heading line 113-191 units off while
  aimed (camera offset). Power attacks/bashes empty stamina in one hit in this load order.
- 0.6.2 (jump aim = game's bow crosshair actor for the player): build-validated only.
- 0.7.0 (retreat/backoff on health only unless `StaminaAffectsRetreat`; power moves need `PowerAttackMinStamina` 0.7,
  brain and vanilla): build-validated only.
- 0.7.1 (MoveNode audit lines via movement Enter/Exit hooks; RE of the leaves' start/end conditions in TREE_DESIGN §10):
  build-validated only.
- 0.7.1, run 12: hooks clean; our Backoff started 2/30, Fallback 15/27 (TREE_DESIGN §10 start rules).
- 0.8.0 (brain Backoff/Fallback start where asked via 7 start-site patches; failed skill roll -> next-best option;
  random ties): build-validated only. Check MoveNode lines: our Backoff/Fallback should be "done", moved > 0.
- 0.8.4 (our Backoff ignores "target closing in"; after-swing backoff in the next-attack window too; Circle start-check audit):
  CRASHED in Circle (get_CircleAngle): the 0.2.4 CircleAngle thunk dropped its target arg; 0.8.4 exposed it.
- 0.8.5: CircleAngle thunk takes (self, target); all hook signatures re-audited. Run 16: stable.
- 0.8.6: our Advance closes to 0.9 x brain attack range (`BrainAdvanceDistance`); INI newline fix. Build-validated only.
  Run 17: stable; Advance still "arrived" 80-99 units short (CheckShouldStop tolerance).
- 0.8.7: our Advance in place only inside the brain stop distance (CheckShouldStop override); trampoline 512 B.
  Run 18: stable, attacks up (light 16-23/min), but our Advance still ended at once (accept band outer).
- 0.8.8: our Advance accept band collapsed to the stop distance (outer = inner). Build-validated only.
  Open: in-range Backoff >> attacks (0x8E1A1: 213 vs 81); bash intents taken rate.
- 0.9.0: Backoff rolled per swing (BackoffOnSwingChance 0.35, BackoffAfterSwingChance 0.4; failed roll -> next-best or
  vanilla); summary names the NPC whose block/bash intents expire untaken.
  Run 19: stable 5.7 min; our Advance done ends 79-117 (avg 97) = stop distance (0.8.8 confirmed). Failed rolls went to Strafe 177/273.
- 0.9.1: failed Backoff roll never becomes another movement (vanilla takes the tick).
- 0.9.2: damage race (press when winning, retreat when losing up to 75% HP), trade-blows roll 0.3, per-swing roll keys fixed (were per tick).
  Run 21: stable 9 min; light 12-21/min early; trades passed 38 ticks (29 then gated by own attack/bash < 0.3 s). Race: player hits NPCs ~10%/hit, NPCs hit player 0.3-1.3% (8E1A0 10%: winning at 81% HP); losing only at HP 1-13%.
- 0.9.3: no Feint for bow/crossbow users (feint = bash, never taken: archer 0x8E1A4 23 untaken).
  Run 22: stable; block/bash never taken 0. Race decides: winning at HP<=30% retreated 0 times (advanced 75), losing -> Retreat 95. Trades 16. Only 88/1066 selected ticks in reach (dist 200-400 mostly), short fight: more data needed.
- 0.9.4 (data audit): target recovery = kHit/kNextAttack/kFollowThrough (was kFollowThrough only: 1/1066 ticks); every combo swing counts as a swing; power-attack flag only while attacking; attack duration uses WeaponSpeedMult (was movement SpeedMult); swing counter survives target switch; blocked hits are neither hit nor miss; no backoff from a swing that already landed. Runtime-evidenced (run 23: recovery 168/1014 ticks).
- 0.9.5: per-NPC temporal timers advance by time since that NPC was last processed (was one frame: ~9x slow; 0.3 s anti-spam gate lasted ~2.7 s, 185 in-reach attacks blocked in run 23). Runtime-evidenced (run 24: DeltaTime 0.109 s, gate blocks 49).
- 0.9.6: EvaluateInterrupt (bash) returns while timeSinceLastAction < 0.3 s, like EvaluateOffense (run 24: bash re-posted each tick mid-bash). Runtime-evidenced (run 25).
- 0.9.7: health and race retreats stop after 8 s total retreating without +5% health from its lowest (retreatTime in temporal self). Threat/outnumbered retreats unchanged. Build-validated only.
- 0.9.8: debug probe on Character NotifyAnimationGraph (VTABLE_Character[3] slot 1): `AnimEvent` lines for attack/bash/block events of the selected NPC. Log-only. Runtime-evidenced (run 26).
- 0.9.9: debug probe on the hit-cone (SE 48139+0x493 -> 46033, AE 49170+0x3F1 -> 47297) and CheckAttack (SE +0x4A6 -> 48140, AE +0x404 -> 49171) calls in StartAttack: `AttackCheck` lines. Log-only. Runtime-evidenced (run 27).
Open (actuator, run 27): of 53 taken attack intents 31 fired; 11 turned down before the checks (4 while sprinting), 8 failed range/path at 56-99 units, 3 failed the hit cone at 57-100 deg. Bash fired 3 of 15 (12 before the checks). The 0.3 s gate counts from the post, so each failed attempt also delays the retry.
- 0.10.0: timeSinceLastAction resets on an accepted attackStart*/attackPowerStart*/bashStart/bashPowerStart graph event (NotifyGraph hook, all NPCs), not on Attack/PowerAttack/SprintAttack/Bash posts; EvaluateOffense skips while sprinting and when facing dot < 0.643 (50 deg). Runtime-evidenced (run 28).
Open (run 28): bash fired 1/12 taken, 10 of 11 refusals at stamina <= 8% (bash stamina cost); 16 attack refusals before the checks all from dagger/bow bandit 0x8E1A4 (6 within 0.7 s of switching to melee, rest unexplained); none of the fast no-check cases had the sprint flag, so the 0.10.0 sprint gate never fired.
- 0.11.0: projectile dodge only for shooters hostile to self (Assess takes the actor; IsHostileToActor); evasion dodge needs TargetAimedAtUs (IsAimedAt or player autoAimActor) and a ranged/power/sprinting target, blocking trigger removed; bash gated on currentStamina >= GMST fStaminaBashBase; offense skip for 1 s after the weapon type changes. Runtime-evidenced (run 29).
- 0.11.1: Intents::DefendUnavailable (3 block/bash posts in a row expired untaken, latest < 10 s) stops EvaluateInterrupt and EvaluateFeinting; feint also needs currentStamina >= fStaminaBashBase. Build-validated only.
- 0.12.0: close dodge in EvaluateEvasion: target swinging, aimed at us (TargetAimedAtUs), melee, not in recovery, dist <= EffectiveAttackRange(target reach, self radius, 1.0) + 30; RollOnce per swing (key bit 31 apart from trade roll) at CloseDodgeChance 0.3; won roll priority >= 3.0. Build-validated only.
- 0.13.0: DetermineDodgeDirection left/right swapped back (clockwise heading vs atan2); DodgeSystem::ExecuteDodge takes ordered options and skips Blocked ones (ledge probe + kLOS ray 35-220 units at +60); evasion: random side, mirror, back; projectile: side, mirror; forward: forward only. Runtime-evidenced (run 30: 5/5 match).
- 0.13.1: won close dodge priority 10; ExecuteDodge ignore ref (forward dodge target); ForwardDodge falls back to Advancing; close-dodge log after the stamina check. Build-validated only.
- 0.13.2: DodgeSystem::Blocked casts centre + two rays at +-22 units (shoulders). Build-validated only.
- 0.14.0: TargetAimedAtUs also gates the evasion strafe on a target swing, EvaluateBackoff's cast/draw and swing triggers, and the offense "target is attacking" skip. Build-validated only.
- 0.14.1: removed the 0.13.1 executor fallback ForwardDodge -> Advancing (CombatDirector already relabels a failed ForwardDodge as Advancing). Build-validated only.
- 0.15.0: EnableParry/EnableTimedBlock true in the shipped INI; EvaluateTimedBlock no longer requires TimedBlockIntegration::IsAvailable, ExecuteTimedBlock applies the window spell only when available; parry and timed block need TargetAimedAtUs; low bash stamina falls through to EvaluateTimedBlock. Build-validated only. Runtime-evidenced (run 32). Timing check: scratchpad parrytime.py and hittime.py.
- 0.16.0: ActorStateObserver::OnHitLanded learns per-attacker swing-start-to-hit (EWMA 0.3, light/power, melee hits incl. blocked) and timeUntilAttackHits uses it after 2 hits (else 0.6 x estimate + 0.24). DefendRoll (DefendOnSwingChance 0.5, key bit 30) gates parry / timed block; won roll priority >= 6. Runtime-evidenced (run 33).
- 0.16.1: HasShield (left hand armor) required for parry and timed block (too strict, replaced in 0.16.2); OnHitLanded skips samples outside 0.5x-2x the average from the 3rd hit. Build-validated only.
- 0.16.2: CanRunBlockTree = shield in the left hand, or empty left hand and a right-hand sword/axe/mace/two-handed weapon (vanilla CombatInventoryItemOneHandedBlock rule, SE 43626/43833/43837, COMBAT_AI_RE.md). Build-validated only.
- 0.17.0: AllowDaggerBlock patches the OneHandedBlock CheckShouldEquip mask 0xFD -> 0xFF (SE 43837+0x74, AE 45056+0x71) after checking the byte; CanRunBlockTree accepts daggers when patched. Build-validated only.
- 0.17.1: EngineProbes::DualWieldBlockProbe (selected NPC, debug log): when EvaluateTimedBlock reaches the window for an NPC without the Block tree, set ActorState2.wantBlocking + blockStart, sample IsBlocking at 0.2 s, release at 0.6 s (wantBlocking 0 + blockStop). Runtime-evidenced (run 34).
- 0.18.0: DualBlock node (Idle clone, vtable copy with Enter/Exit) first in the Melee tree and, via the Magic SetRoot hook (SE 48065+0x4AC, AE 49112+0x7AC), in the Magic tree; Intents dual-block hold state (also counts as pending, so vanilla attack branches wait); ExecuteTimedBlock posts it for BlockRules::CanDualBlock NPCs (hold = time to hit + 0.25 s, 0.3-1.0); offense/bash skip while held; brain-side release when overdue or on evict. BlockRules.h holds CanRunBlockTree/CanDualBlock. Runtime-evidenced for the Melee tree (run 35); Magic splice failed the shape check.
- 0.18.1: Magic splice puts NextSelector{DualBlock, child} under every single-child Repeat found (depth <= 16); executor keeps a held dual block through movement; no feint while held. Build-validated only.
- 0.18.2: cast node Update hook (vtCastImmediate SE 266704 / AE 213718, vtCastConcentration SE 266706 / AE 213733, slot 4): Ascend when WantsDualBlock; their Exit interrupts the cast. Build-validated only (no Magic splice yet, so never triggered).
- 0.18.3: one-time Tree dump of the vanilla Magic tree at SetRoot (run 36: no vtRepeat node found under the root). Runtime-evidenced (run 37 dump).
- 0.18.4: Magic splice under ValueNodeT<CastingType> cases (SE 266709 / AE 213739); cast-stop hook on all four cast vtables (single SE 266705/266707 AE 213720/213735, dual SE 266704/266706 AE 213718/213733). Runtime-evidenced (run 38).
Open (run 38, sword+spell Bandit Chief): 57 player hits, blocked 2. 29 landed while it was attacking itself, 20 with no defend evaluation in the window, 4 lost the defend roll, 2 won it but the hit got through.
- 0.18.5: parry/timed-block min distance 0; HitDelay = median of last 9 per attacker and kind (replaces EWMA + outlier filter); EvaluateInterrupt returns EvaluateTimedBlock right away for NPCs without the Block tree (no bash posts, DefendUnavailable not reached); feint needs the Block tree. Runtime-evidenced (run 39).
- 0.18.6: dual block posts a 1.2 s hold; CombatDirector ends it (Intents::EndDualBlockHold) when the target stops attacking or enters recovery. Contradicted (run 40: recovery ended 30 holds ~0.15 s in, 5/73 blocked).
- 0.18.7: hold ends on the melee hit event on the NPC (ModEventSinks) or when the target stops attacking (no recovery check); 1.2 s cap. Runtime-evidenced (run 41).
Open (run 41): 53 player hits on a sword+spell Bandit Chief, blocked 2; 37 landed while it was attacking itself (trading blows), 8 lost the defend roll. Block rate is now a decision question (trade vs defend), not the actuator.
- 0.19.0 (audit fixes, build-validated only): targetJustFinished (not attacking, idle < 0.4 s) replaces timeSinceLastAttack 0.1-0.5 s in EvaluateOffense; powerAttackBonus "= 0.3" -> "+= 0.3"; power attack only with a positive total bonus; Advancing and SprintAttack gates use TargetAimedAtUs; outnumbered keeps the fleeing pursuit boost; AttackCoordinator rest 1.5 s after a slot expires while others are held.
Open (audit, choices for the user): EvaluateFlanking validates closestAllyPosition by its world magnitude (|pos|^2 > 1e7 returns), so it never runs outdoors; flanking seen in logs is the pacing hold (EvaluateHeldBack). Feint's targetIsDefensive includes "facing us and not attacking". Attack scoring gives +0.3 for a blocking target.
- 0.19.1: all three done (flanking uses closestAllyDistance <= 3000; feint needs a blocking or frequently blocking target; +0.3 blocking bonus removed from normal and sprint attack). Build-validated only.
- 0.20.0: our guard counter: TESHitEvent kHitBlocked on an NPC opens a 0.6 s window (ActorStateObserver::m_counterUntil), OR-ed into isGuardCounterActive with EldenCounter Val_GuardCounter. Runtime-evidenced (run 42: 9 windows, counters at 5.3-6.7).
- 0.20.1: EvaluateFlanking returns when already in flank position (dot < 0.3), maintain rules removed, capped at 1.4 in attack range; counter window skips the trade-blows gate. Runtime-evidenced (run 43): still passive.
- 0.20.2: TimedBlock/Parry no longer reset timeSinceLastAction; counter window 0.2-1.0 s after the block; facing gate 75 deg; flanking cap 1.4 within 1.5x reach; MaxSimultaneousAttackers 3. Runtime-evidenced (run 44).
SCAR research (subagent, source e62b63a, MIT): data = clip annotations `SCAR_ActionData{json}` read from active hkbClipGenerators; first attack hooks SE 48147+0x164 (replaces StartAttack, never calls the original); combos via VTABLE_Character[2] slot 1 on SCAR_ComboStart, needs a running node named "attack" (ours is); no API. Option c (parse annotations ourselves, 1-2 days, SCAR.dll removed) deferred.
- 0.21.0: RangedMovement SetRoot hook (SE 48476+0x495, AE 49468+0x957) with the shared SpliceMovement; g_moveSelectors 8; IsRangedFighter (bow/crossbow/staff/spell) skips Interrupt/Offense/Flanking/Feint and runs EvaluateRangedPositioning (Retreat below RangedKeepAwayDistance 450 vs a melee target, Advancing with holdDistance beyond RangedMaxDistance 2000 or no LOS); ExecuteAdvancing uses holdDistance as stop distance when set; WEAPON_TYPE 8/9 (staff/crossbow) swap fixed. Build-validated only. Not done: switching to a melee weapon when cornered (vanilla inventory decides). Note: kiting Retreats count toward the 8 s cornered timer. Runtime-evidenced (run 45).
- 0.21.1: EvaluateRangedPositioning returns None below 150 units from a melee target (contact). Build-validated only. Open: Fallback in RangedMovement failed at start 23 of 29 (moved 0; Circle failures there were path checks); no right-hand spell/staff caster tested yet.
- 0.21.2: no ranged fall-back while self attackState != kNone (bow draw / cast). Build-validated only. Run 46: spell caster tested (WeaponType 11), fallbacks moved 76-81.
- 0.22.0: Ranged tree chance lea (SE 32398+0xA3 -> 49772, AE 33149+0x111 -> 50700) redirected through a trampoline stub to RangedShotChance = vanilla x Intents::RangedShotFactor (CombatDirector posts DecisionMatrix::RangedShotFactor each tick for bow/crossbow: 0 ally in line, 0.3 guard facing, 2 opening); EvaluateRangedPositioning strafes aside when AllyInLineOfFire (closest ally within 55 units of the line). Build-validated only. Not done: mage cast timing (SE 48065 has one lea of 48048 only; AE two of 49095). Runtime-evidenced (run 47).
- 0.22.1: RangedShotFactor opening no longer includes target orientationDot < 0.2. Build-validated only.
- 0.22.2: CombatContext.allyPositions[8] filled by the ally scan, AllyInLineOfFire checks all; Fallback start audit (SE 46712 +0x15F anim-driven / +0x27B straight path, AE 47908 +0x12B / +0x239); StartAttack stamina-cost probe (SE 48139+0x29B -> 25863, AE 49170+0x27A -> 26429). Build-validated only.
- 0.23.0: Magic tree cases get NextSelector{DualBlock, Hold Cast (Idle, Intents::RangedShotFactor <= 0), vanilla}; CombatDirector posts the factor for non-archers as AllyInLineOfFire ? 0 : 1; CastUpdate stops concentration casts (K 1/3) on hold. SCARData (include/SCARData.h, src/SCARData.cpp): flat-JSON parser + SelfTest, Read (graphs[0] activeNodes, first hkbClipGenerator with SCAR_ActionData, under the manager's updateLock), InRange (SCAR rule without the AABB height check), Probe (debug, selected NPC, on Attack/PowerAttack decisions or kNextAttack). Build-validated only.
- 0.23.1: Intents::PostRangedShot caps a hold at 3 s, then 1.5 s at factor 1 (Entry.holdSince), returns the posted factor. MoveExit<Circle> notes a failed run of our Circle (Intents::NoteCircleRefused: side + time); EvaluateRangedPositioning flips the step-aside side when that side failed under 2 s ago (Intents::CircleRefused), and an ally in the line within the max distance beats the no-sight Advance. Build-validated only.
- 0.23.2: ApplyCombatPacing releases and skips any NPC outside reach + engageMargin that IsRangedFighter (now public DecisionMatrix static), even mid-draw; shot factor TTL 0.4 s -> 1 s. Build-validated only.
- 0.24.0: SCARData::InstallGate (from Hooks::Install at kDataLoaded, after SCAR's kPostLoad hooks): write_call at SE 48147+0x164 / AE 49178+0x148 (outer wrap of SCAR's StartAttack hook; returns false when Refusal() and the active clip has SCAR data, else calls through) and write_vfunc VTABLE_Character[2] slot 1 (swallows SCAR_ComboStart on Refusal()). Refusal: AttackCoordinator::IsHeld, Intents::WantsDualBlock, Intents::AttackVetoed (posted 0.4 s by ExecuteDecision for Dodge/Parry/TimedBlock/Retreat/Backoff/Jump). ActorStateObserver: weaponReach = (SCARData::AttackReach + margin) / mult on a SCAR clip, baseWeaponReach keeps the weapon's. Build-validated only.
- 0.25.0: ComboFollowUp (include/ComboFollowUp.h, src/ComboFollowUp.cpp): VTABLE_Character[2] slot 1 hook (only without SCAR.dll; the SCAR gate uses the same slot with it) tracks MCO/BFCO window annotation events per actor (1.5 s stale cap); CombatDirector calls Update after pacing: one roll per window, power first, TESActionData{source, target, kActionRightAttack / kActionRightPowerAttack, animObjIdle + animEvent = ADXP_NPC*Attack idle when found} on an SKSE task. Config [ModIntegrations] EnableComboFollowUp / ComboFollowUpChance 0.7 / ComboPowerChance 0.3. Build-validated only.
- 0.25.1: ComboFollowUp::Perform = CombatAnimation::Create(actor, kActionRightAttack / kActionRightPowerAttack), target, animEvent "attackStart" / "attackPowerStartInPlace", Execute (SKSE task); idle lookup removed. Update called after the Backoff roll in ProcessActor (also on the "None instead" return). NOTE: the build uses D:\Modding\commonlibsse-xmake\CommonLibSSE-NG (xmake.lua COMMONLIBSSE_NG_DIR), not lib\CommonLibSSE; check APIs there. Build-validated only.
- 0.26.0: Personality (include/Personality.h, src/Personality.cpp): risk/caution/patience from kConfidence, kAggression, CombatStyle offensive/(offensive+defensive) and avoidThreatChance, FormID jitter +-0.1, cached 10 s; RiskScale = 0.25 + 1.5 risk. Forward dodge (DecisionMatrix advancing block): facing-away opening dropped, guard check, RollOnce per 1.5 s bucket with ForwardDodgeChance x RiskScale (x2 counter, x0.5 exposed unless risk >= 0.7), ForwardDodgeMinStamina 0.5, Humanizer cooldown ForwardDodgeCooldownSeconds 8. Sprint attack: RollOnce per 2 s at 0.4 + 0.6 risk. Trade blows x RiskScale. Build-validated only.
- 0.27.0: CautionScale = 0.6 + 0.8 caution on backoffAfterSwing/OnSwing (CombatDirector "pressing"), DefendRoll, close dodge; feint RollOnce per 2 s at 0.4 + 0.6 patience; weights risk 0.45 conf + 0.35 aggr + 0.2 offense, caution 0.45 (1-conf) + 0.2 (1-offense) + 0.35 avoid, patience 0.7 (1-aggr) + 0.3 (1-offense), jitter +-0.15; combo follow-up range 1.3x EffectiveAttackRange. Build-validated only.
- 0.28.0: EvaluateFlanking returns none beyond 2.5x EffectiveAttackRange or when TargetAimedAtUs (the target faces us); reach computed once above. Pacing's EvaluateHeldBack (waiting NPCs) unchanged. Build-validated only.
- 0.29.0: Boss (include/Boss.h, src/Boss.cpp): marker = actor->extraList ExtraLocationRefType == LookupByID<BGSLocationRefType>(0x000130F7) (Skyrim.esm LCRT EDID "Boss", verified by parsing Skyrim.esm read-only; BossContainer 0x130F8, BossTreasureMarker 0x130F9); named = GetName() in [Boss] BossNames (case-insensitive, comma list); cached per FormID; called each brain tick (cached) and on console selection. 0.29.1: + "boss" in GetFormEditorID of GetActorBase / baseTemplateForm chain (4) / ExtraLeveledCreature originalBase, templateBase; EditorIDsLoaded() tests TESNPC 0x000E1646 == "LvlBanditBossMelee2HNordM". No behavior change. Build-validated only.
- 0.30.0: Boss::UpdatePhase (CombatDirector, health thresholds 0.5 / 0.25, info log on change), CurrentPhase, Brave (Confidence >= 3). Personality::Effective = Get + boss phase (caution floor 0.4; enraged risk +0.2; desperate brave risk 1 / caution 0.25, careful risk x0.5 / caution 0.9); all brain rolls use Effective. ApplyCombatPacing: boss releases/never holds a slot. Humanizer::SkillChance +0.15 for bosses. DecisionMatrix::EvaluateBoss: desperate careful Retreat 2.0 (ally behind), target healing (TargetHealing: caster currentSpell costliest effect, non-hostile Health value modifier) Attack 2.8 / sprint Advancing 2.2, guard break (block >= 1 s, in reach, facing) PowerAttack 2.6 / Bash 2.4, anti-kite (ranged/casting/drawing, 2x reach .. 1500) sprint Advancing 1.9. Offense: enraged powerAttackBonus +0.3. ComboFollowUp: raging x1.3 / x1.5. 0.30.1: Boss::PowerStaminaMin (min(cfg, 0.4) for bosses) in ExecuteDecision, ComboFollowUp, VanillaAttackShaper, EvaluateBoss; guard-break bash needs dist <= BashReach() (GMST fCombatBashReach) x scale + target radius; survival Retreat dropped for enraged / brave-desperate bosses. Build-validated only.
- 0.31.0: ShouldProcessActor takes ActorTypeNPC (non-creature) or, with IncludeHumanoidCreatures, ActorUtils::IsHumanoidCreature (ActorTypeCreature + right-hand TESObjectWEAP not hand-to-hand, race not GiantRace 0x131F9 / C00GiantOutsideWhiterunRace 0xCAE13 / DragonPriestRace 0x131EF); info log once per race. DecisionMatrix drops Dodge/Jump/ForwardDodge results and the forward-dodge gap close for them. 0.31.1: Intents::TakeDefend(Bash) clears e.committed (bash candidates were zeroed by VanillaAttackShaper's committed-kind filter). Build-validated only.
Next: log run with draugr (a deathlord) and the 0.30.1 boss checks: `Brain: humanoid creature` lines, rejected anim events for draugr (bashStart, block), deathlord Boss + phase lines.
Next (if accepted): custom block leaf (TREE_DESIGN 6, Idle clone) in the Melee tree for dual wield, then the Magic tree (needs its builder hook, SE/AE RE).
Finding (#4, RE notes COMBAT_AI_RE.md ActionEquipmentSelector): the Block tree (bash, block, parry, timed block) is linked only for equipment type 3 Shield. NPCs without a shield never run it, so their bash/block/parry/timed-block intents can't be taken (0x8E1A4 with a dagger, archers). DefendUnavailable (0.11.1) stops the requests after 3. Whether two-handed or one-handed weapon blocking goes through another path is unknown.
Open (run 29): selected-NPC attacks fired 44 of 74 taken; 12 refused before vanilla checks, mostly within 0-3 s of the previous attack (per-attack cooldown suspected, not probed); range/path FAIL 16.
Open (actuator, run 25): taken attack intents started a swing within 1 s 14/33, bash 6/25 (0/4 at 0 stamina); dodge events all rejected (TK Dodge not installed, expected); feint/dodge/jump still on the old executor; block intents never posted; feedback channel (roadmap 0.3.0) not built.
  Run 20 (0.9.1): stable 8.8 min; failed rolls -> None 128/132. Healthy in-range NPC: Attack candidate 74 ticks, 54 lost to failed skill rolls (skill 62-69%, 1 s reroll window). Retreat 443 = all at 10-29% HP (health rule).
  Open: in range the gate `target is attacking` blocks Attack on 147 ticks, `last action too recent` 131 (after own attack/bash).
- 0.8.1: package = DLL + INI only (OAR jump meshes and BDI json removed with the jump's OAR fallback).
- 0.8.2: Backoff during own follow-through when the target isn't open (`BackoffAfterSwing`): build-validated only.
- **0.8.1 CRASHED** (CTD, no crash log) approaching an NPC. Rollback 0.7.1. 0.8.3 fixes the shared candidate-list race and the empty tie pick; start patches only with BrainBackoffFallback=true. Isolation plan in VALIDATION.md.

**Next (for the user):**
1. Install 0.6.0. A menu-saved INI keeps `SkillBase = 0.55`: set "Skill at level 1" to 0.75 in the menu (load log shows it). Earlier: Turn on "Enable jump evasion" in the menu (or `EnableJumpEvasion = true`, chance 1.0); the log line `Jump evasion: on` confirms. Fight bandits with one selected. For the jump, **the player** must hold a bow drawn and aimed
   at the selected NPC, inside the JumpEvasionDistance window. Check:
   - **0.5.2:** no `Move ... stopped` for the same actor every frame; short-weapon NPCs swing when touching you;
   - **0.4.0 movement:** the summary's `running moves stopped: ours N, vanilla N` is above 0, no foot stutter (no
     `Move ... stopped running vanilla node` for the same actor every 0.3 s), no hang;
   - **0.5.0 jump:** set `EnableJumpEvasion` / `EvasionJumpChance` high in the INI to see it often. Look for
     `Jump 0x..: side arc ...` then `landed`, and watch whether the leap looks right: no sliding pose, no fall
     damage, no bounce, no stuck jump state;
   - **circle side** by eye against the `Circle ... (brain side +1)` line (+1 = to its own left).
   - If 0.5.0 misbehaves, 0.4.0 is the same build without the jump change.
2. Then (dev): Surround + fallback distance parameters. The custom leaf is parked (TREE_DESIGN §6).
**Last good version:** 0.8.5 (runtime-evidenced, run 16). Then 0.8.3 (run 14). Then 0.7.1 (run 12), 0.5.3 (run 9). Then 0.5.2 (run 8). Then 0.3.2 (run 6). Then 0.3.1 (run 5), 0.3.0 (run 4). Before that 0.2.2, then EnhancedCombatAI 1.10.2.
**Log:** `Documents\My Games\Skyrim Special Edition\SKSE\CombatAITree.log`
(debug lines need `EnableDebugLog = true` under `[General]` in `CombatAITree.ini`, the shipped default;
they cover the actor selected in the console).

**Build:** `xmake f -m releasedbg --skyrim_se=y --skyrim_ae=y --skyrim_vr=n -y`, `xmake -y`, then
`pwsh scripts/package_release.ps1 <version>`. Output DLL: `build/SkyrimSEAE/skse/plugins/CombatAITree.dll`.