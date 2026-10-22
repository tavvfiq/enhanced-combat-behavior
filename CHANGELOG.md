# Changelog

## CombatAITree 0.9.0 (2026-10-22)

- **Less passive: backing off is a choice, rolled once per swing.**
  - **Problem:** in run 18 one bandit in range backed off 213 times against 81 attacks. About 155 of those were "you swing, it steps back" (any of your swings within `SprintAttackMinDistance`) and about 55 were its own after-swing step-out.
  - **Rolls:**
    - `BackoffOnSwingChance` (0.35): one roll per normal swing of yours. It keys on your swing's start time, like the Humanizer's defense rolls. Power attacks are always allowed.
    - `BackoffAfterSwingChance` (0.4): one roll per swing of its own, in the next-attack window or follow-through.
    - Pacing waits ("waiting its turn", flagged `paced`) are not rolled.
  - **On a failed roll** the NPC takes the next-best option of that tick, the same way a failed skill roll does: through pacing, never a pull-back mid-swing. If there is none, it leaves the moment to vanilla (often a block, or another swing).
  - Both chances are in the menu. Debug line: `no back off this swing (target's swing|own swing) - X instead`.
- **Bash diagnostic:** the per-minute summary adds `block/bash never taken: N (most 0x.. xM)`. A block/bash intent that expired and was posted again counts once, so the NPC whose bashes the tree never runs shows up even when it isn't selected. For selected NPCs in run 18 the tree took 37 of 38 bashes; the untaken ones in the summaries came from unselected NPCs.
## CombatAITree 0.8.8 (2026-10-22)

- **Our Advance actually closes in.**
  - **Problem:** in run 18, our Advance "finished" 41 times at about 129 units, 25 ms after starting, having moved 0. The bandit's stop distance was about 93 (reach 99.8). The Advance start also builds an accept band of [inner, outer] + weapon range (SE 46703 +0x38F), and 0.8.6 only moved inner. With vanilla's outer (256-512 + range) the NPC was already inside the band, so the advance counted as done on the next tick.
  - **Fix:** for our Advance, outer = inner as well, so the only accepted spot is the brain's stop distance.
## CombatAITree 0.8.7 (2026-10-22)

- **Our Advance doesn't stop short of the brain's range.**
  - **Problem:** in run 17 the new ring was set, but the vanilla "in position" check (`CheckShouldStop`, 46698/47893, called at Advance Update 46704/47899 +0x121 on all three runtimes) accepts anywhere within its own tolerance of the destination. A bandit with attack range ~76 "arrived" 34 times at 80-99 units, at 24 ms, having moved 0.
  - **Fix:** for our Advance (with `BrainAdvanceDistance` on) the NPC is in position only once it is within the brain's stop distance of the target. Otherwise vanilla's check decides.
- **Hook space:** the trampoline is raised from 256 to 512 bytes. 17 hooks use about 238 B.
## CombatAITree 0.8.6 (2026-10-22)

- **The brain's Advance closes into its own attack range** (`BrainAdvanceDistance`, default on, in the menu, installed at load).
  - **Problem:** in run 16 a bandit with reach 82 (brain attack range ~86) parked at 125-175 units. The vanilla Advance start (46703/47898) aims at a ring of "engine weapon range + inner radius", which is longer, so our Advance "finished" 105 times at 24 ms without moving. The NPC then stood outside its own range and the brain never attacked.
  - **Fix:** the brain posts a stop distance with the Advance intent: 0.9 × its attack range. While our node starts, the inner radius from `set_two_radiuses` (SE 46703+0x16F, AE/1.7 47898+0x127) is set so the ring lands there. The location search and the approach controller both use that ring, and the "in position" check (`CheckShouldStop`, 46698/47893) then judges against the new spot.
  - **Intents:** `PostMove`'s third argument is now "Circle side, or Advance stop distance". `Intents::AdvanceDistance()` is new.
- **INI fix:** a missing newline in the 0.8.2-0.8.5 INI glued `BackoffAfterSwing = true` onto the next comment. It still read as true. The file is fixed and every key line has been checked.
## CombatAITree 0.8.5 (2026-10-22)

- **Fixed the 0.8.4 CTD in Circle** (1.7.104, `SkyrimSE.exe+08F1FCD` inside `get_CircleAngle` 50648, `subss xmm2, [rbx+0x54]` with rbx = 0x25).
  - **Cause:** the Circle-angle hook (since 0.2.4) was declared `float(Actor* self)`, but the getter is `(self, target)` and reads the target from rdx (SE 49721 and 1.7 50648 both `mov rbx, rdx`; the callers pass `mov rdx, r15`).
  - **Why it surfaced now:** before 0.8.4 the thunk called the original first and left rdx untouched by luck. 0.8.4 put the audit's thread-local write in front of that call, which clobbered rdx.
  - **Fix:** the thunk now takes and forwards both actors.
- **Re-checked:** every other hooked callee's argument registers against its thunk (46062, 46063, 36487, 36756→36757, 49716, 49718, 32513, 32507). All match.
## CombatAITree 0.8.4 (2026-10-22)

- **Our Backoff keeps going while you press in.** The vanilla Backoff update (46725/47921) ends the node as soon as 46063 sees the target moving toward the NPC. In run 14, 24 of our Backoffs ended after about 110 ms having moved 6 units. For our branch that check now says no.
  - The callee has no stable ID on 1.7 (47327 on AE, 523985 on 1.7). It is accepted as a call into the function right after the start's 46062, which is 0xB0 later on SE/AE and 0xE0 on 1.7.104.
  - It is installed with the 0.8.0 start patches, so `BrainBackoffFallback = false` at load leaves it out.
- **Back off after a swing also in the next-attack window.** NPC swings go hit, then next attack, then none; follow-through showed up in 1 of 968 snapshots, and `BackoffAfterSwing` fired once.
  - The trigger now fires in either window.
  - The director no longer blocks Backoff/Retreat in the next-attack window, since the hit has landed. It still blocks them in windup, swing, hit and bash.
  - The failed-roll mistake path follows the same rule.
- **Circle start audit:** for the selected NPC, every Circle `MoveNode` line ends with what its start checks answered. 1 = passed, 0 = refused, -1 = not reached. Example: `[start checks: not animation-driven 1, facing 1, angle 1, view cone 0, straight path -1, path -1]`.
  - **Checks recorded:** IsMovementAnimationDriven, the two WithinHeadingAngle calls, the angle getter being reached, CheckStraightPath and the path check after it. All of them pass through unchanged.
  - **How to read it:** "angle 1, view cone -1" means it stopped at the angle limit. Everything passed but it still failed means navmesh.
## CombatAITree 0.8.3 (2026-10-22)

- **Fixed the 0.8.1 crash when approaching an NPC** (no crash log; the mod log ends at data load).
  - **Main suspect:** 0.8.0 kept the brain's candidate list in one vector shared by every actor update. Actors are updated on several threads, so two NPCs entering combat could write it at once and corrupt the heap. Heap corruption kills the process before a crash logger can run. The list is now per thread.
  - **Also fixed:** 0.8.0's random tie-break indexed an empty list when no candidate reached the top priority (all priorities below -1, or NaN). It returns None again, like the old tie-break.
- **Isolation lever:** the seven Backoff/Fallback start patches are installed only when `BrainBackoffFallback = true` at load. The log line `BrainBackoffFallback off at load - ... not hooked` confirms when they are not.
## CombatAITree 0.8.2 (2026-10-22)

- **Back off after its own swing** (`BackoffAfterSwing`, default on, in the menu). During the follow-through of its own attack, with the target still in reach and not open to a follow-up (recovering from its own swing, or staggered), the brain now picks Backoff.
  - **Why:** offense and evasion only run when the NPC's attack state is none, so during follow-through the brain had nothing to offer, and the NPC stood in the counter until recovery ended.
  - **Gates:** the director already allows Backoff in follow-through (only windup, swing, hit and bash block it), and our Backoff start (0.8.0) steps back `BackoffStepDistance`.
  - **Still vanilla:** vanilla's "no move while an animation moves it" check still applies, so if the swing animation drives movement, the step waits for it.
## CombatAITree 0.8.1 (2026-10-22)

- **Package trimmed to the DLL and the INI.**
  - **Removed:** the OAR "jump as dodge" folder (`meshes\...\OpenAnimationReplacer\CombatAI NG`, 7 hkx and 2 configs) and `SKSE\Plugins\BehaviorDataInjector\EnhancedCombatAI_BDI.json`. Both only served the old jump (0.4.x and earlier).
  - **Code:** the jump's OAR fallback is gone too. It set `EnhancedCombatAI_Jump` and played a TK dodge when the graph refused the jump events.
  - **Now:** if the graph takes neither `JumpDirectionalStart` nor the standing jump, there is no jump, and the debug log says `graph took no jump start event`. Runs 9-12 never needed the fallback.
- **Upgrading:** delete the old folders if your mod manager kept them: `meshes\actors\character\animations\OpenAnimationReplacer\CombatAI NG` and `SKSE\Plugins\BehaviorDataInjector\EnhancedCombatAI_BDI.json`.
## CombatAITree 0.8.0 (2026-10-22)

- **The brain's Backoff and Retreat now start where it asks** (`BrainBackoffFallback`, default on, also in the menu).
  - **Problem:** in run 12 our Backoff node started 2 times in 30 and Fallback 15 in 27. The vanilla start functions (Backoff 46724/47920, Fallback 46712/47908) refuse unless both actors face each other, the target isn't moving away, and the NPC is already inside a ring of "weapon range + X" around the target. The NPC then stood still; this was 60% of the idle ticks.
  - **Fix:** while *our* branch's node starts, the facing and moving-away checks pass, and X is set so the ring sits a step behind where the NPC stands.
    - Backoff steps back by `BackoffStepDistance` (150, in the menu).
    - Fallback steps back by the vanilla fallback distance.
  - **Unchanged:** vanilla's own Backoff/Fallback, and for our nodes the path request, `CheckStraightPath` (no move into a wall) and `IsMovementAnimationDriven` (no move while an animation moves it).
  - **Patch:** seven call sites inside the two start functions, checked against their callees before patching. The offsets are the same on AE 1.6.1170 and 1.7.104. 46062 was renumbered on 1.7 (523984), so it is matched by being the same target in both functions. If anything differs, the log says so and nothing is patched.
- **A failed skill roll is a mistake, not a freeze.** The NPC takes the next-best option the brain scored that tick, through the usual attack pacing, and never a Backoff/Retreat mid-swing. Only if there is no other option does it do nothing as before.
  - Debug line: `failed skill roll on Attack (...) - Strafe instead`.
- **Equal-priority ties are a coin flip.** The tie heuristic (`SelectBestFromTie` / `CalculateDecisionScore`, 180 lines) is removed, so how good the pick is comes from the skill roll.
## CombatAITree 0.7.1 (2026-10-22)

- **Movement node audit:** for the selected NPC, one debug line per vanilla movement node run (Advance, Circle, Fallback, Backoff):
  `MoveNode 0x..: our|vanilla|other-tree <kind> ran N ms, moved M, dist A -> B, done|failed|aborted|stopped by brain (brain wanted X)`.
  It shows whether a brain move really moved the body, and why a node ended.
- **How:** Enter (vtable slot 2) and Exit (slot 3) of the four classes are hooked next to the existing Update hook. They are called as `(node, thread)` and their return value is unused (checked on SE, AE 1.6.1170 and 1.7.104).
- **RE findings** (TREE_DESIGN §10): the vanilla Backoff only starts inside 0.75 × (weapon range + inner radius) of the target, and only steps out to that ring. Circle runs between ½ × range and range + 1024 and needs a 64-unit move. A failed start ends the node on the next tick without moving. This explains run 11's "Backoff mostly still".
## CombatAITree 0.7.0 (2026-10-22)

- **Retreat and backoff depend on health only.** Light attacks cost no stamina, so a tired NPC keeps fighting. Low stamina no longer triggers the survival back-off (0.6.1), the extra Backoff urgency, or tactical spacing. The new `StaminaAffectsRetreat` (`[DecisionMatrix]`, default `false`, in the menu) turns the old behaviour back on, using `StaminaThreshold`.
- **Power moves need stamina:** new `PowerAttackMinStamina` (default 0.7, in the menu; 0 = no limit).
  - **Brain:** below it, a PowerAttack becomes a light Attack, and a SprintAttack becomes ordinary closing in.
  - **Vanilla:** the tree's power attack data, including power bashes, is not picked below it (debug: `power: stamina too low`). Light attacks and plain bashes are never limited.
  - **Why:** in run 11 one power attack or power bash took a bandit's stamina from 25-100% to 0, and the bandit then spent most of the fight backing off (463 Backoff, 10 Attack).
- Removed 0.6.0's "no bash below 20% stamina" gate. Only power bashes care about stamina now.
- **Load log:** `Stamina: retreat/backoff on health only; power ... need 70% stamina`.
- Both are new INI keys, so an existing menu-saved INI picks up the defaults.
- Not changed: fatigue (`FatigueStaminaThreshold`) still makes skill rolls sloppier at low stamina, and feints still need 25%.
## CombatAITree 0.6.2 (2026-10-22)

- **Jump fires when you really aim at the NPC** (run 11). Your body's heading misses the bow crosshair by the third-person camera offset: aimed straight at the bandit, the heading line passed 113-191 units from it (more the farther away), over the 99-unit limit, so it never jumped. For the player the jump now asks the game which actor the bow crosshair is on (`PlayerCharacter` info runtime data `autoAimActor`, version-shifted in CommonLib-NG for SE/AE/1.7). The strict heading check stays for NPC shooters and as a fallback, so neighbours still don't qualify on it.
- `Jump aim` debug line adds `crosshair on us true|false`.
## CombatAITree 0.6.1 (2026-10-22)

- **Low stamina backs off instead of running away** (run 10). One brain power attack took a bandit from 100% to 17% stamina (a stamina-heavy setup), and stamina came back at about 2%/s. Below `StaminaThreshold` (0.2) survival chose Retreat, the vanilla Fallback run, so a full-health bandit spent 229 decisions (~25 s) running off. Exhaustion now chooses Backoff, a short step back that keeps it in the fight. Low *health* still retreats.
- **Debug:** `Jump aim 0x..: shooter drawing at D, aim line L units from us (limit X)` for the selected NPC while you draw within the jump distance window, to tune the aim slack (75). The log's facing value is rounded to two decimals, which at 800 units hides about ±100 units.
## CombatAITree 0.6.0 (2026-10-22)

- **NPCs attack more:** `SkillBase` 0.55 -> 0.75 (skill at level 1, before fatigue/panic). Run 9 dropped 81 Attack decisions at 33-55%. Presets moved up the same way: Vanilla+ 0.5, Soulslike 0.85, Chaos 0.8.
- **No action cooldowns.** `BashCooldownSeconds`, `DodgeCooldownSeconds` (shared by forward dodge and strafe), `SprintAttackCooldownSeconds` and `JumpCooldownSeconds` are removed from the INI, the menu and the presets. The NPC's own state now limits repeats:
  - dodge: `DodgeStaminaCost` (10) and `MinDodgeStamina`, as before;
  - jump: new `JumpStaminaCost` (20, `[DecisionMatrix]`, in the menu), checked by the brain and spent at take-off;
  - bash: none below 20% stamina (before, low stamina only lowered its priority), never mid-swing;
  - sprint attack: still needs a sustained sprint first.
  The 0.15 s hold that lets a timed block settle stays.
- **Jump only for the NPC you aim at.** Before, every NPC within ~25 degrees of your facing could jump (dot > 0.9), so three bandits jumped when you aimed at one. Now your aim line must pass within the NPC's body radius + 75 units, and you must be drawing (a bow draw reads as attacking). The rule has compile-time self-checks.
- The jump keeps facing you while it leaps sideways (unchanged, on purpose: it reads as dodging the arrow).
- Load log: `Jump evasion: ... stamina N); skill at level 1 X`.
## CombatAITree 0.5.3 (2026-10-22)

- **Fixed: melee NPCs held back while you drew a bow next to them** (run 8). The offense gate "target is attacking, evade instead" also fired on a bow or crossbow draw, which skipped 92 of 115 in-range attacks against an archer player. A ranged target no longer blocks the attack; its draw is the opening.
- **Log:** the jump settings are printed at every config load (`Jump evasion: on|OFF (chance, distance window, height)`). The shipped INI has `EnableJumpEvasion = false`.
## CombatAITree 0.5.2 (2026-10-22)

- **Fixed: movement stop looped every frame** (0.4.0 regression, run 7). With the brain wanting Circle at ~900 units, our Circle branch failed at once, vanilla Advance started, and the hook stopped it again on the very next frame (~80 stops/s for one bandit). Two causes: the 300 ms minimum run kept the node's *first* start time, so re-entries were stopped at once; and a vanilla node was stopped even though our branch for that move had just failed.
  - **Now:** a running move is stopped only if the brain wants a *different* move than when that move started. A stop resets the timer, so the next move always runs at least 300 ms. The rule has compile-time self-checks.
- **Fixed: NPCs with short weapons stood in your face without swinging** (run 7, bandit with Precision reach 48). Precision's capsule length is the blade alone, but every distance is measured from the actor's centre. The attack range was 48 + target radius 24 − margin 20 = 52 units, closer than two bodies can get, so the brain kept choosing Advance while the NPC was already touching you. The reach now adds the attacker's own body radius (the blade starts at the hand): attack range 76 units for that bandit, and 140 instead of 116 for a sword with reach 112. This affects every range that uses reach (offense, range bands, CPR stop distance, attack feedback).
## CombatAITree 0.5.1 (2026-10-22)

- **Config menu shows up in SKSE Menu Framework.** The vendored menu header looks up `SKSEMenuFramework.dll` when our DLL loads. SKSE loads plugins alphabetically, so `CombatAITree.dll` loads first and the lookup returned null. Every menu call then silently did nothing, while the log still said "Registered". The handle is now looked up again at kDataLoaded, and a missing framework is logged as an error.
- The menu section is renamed from "Enhanced Combat AI" to **"Combat AI Tree"** (entry "Config"), so it can't merge with combat-ai-ng's.
## CombatAITree 0.5.0 (2026-10-22)

- **Arc jump, after FluidMotionNPC's NPC jumps.** This is our own implementation of the method; no code is copied (FluidMotionNPC is GPL-3).
  - **Before:** the jump was one upward velocity push, then physics. On AE that needed up to 8 re-pushes because NPC movement kept cancelling it, its height was approximate, it went straight up only, and nothing checked the space around it.
  - **Planning:** the jump (still the archer evasion) now plans a ballistic arc first: a 150-unit sideways leap out of the shooter's line (random side), then the other side, then straight up. Apex is `JumpHeight` and gravity is 900 units/s².
  - **Checks:** each arc is ray-checked at shin, waist and head height for walls and a ceiling. The landing spot needs ground within `DodgeMaxDrop`, at most half the apex higher, and not on another actor. If no arc is clear, no jump.
  - **Driving:** while airborne the actor is placed on the arc every frame, with the matching velocity, the controller held in-air and the fall height reset, so landing is no fall.
  - **Landing:** `JumpLand` at touchdown; 0.35 s with no upward velocity (no bounce); one `JumpLand` retry 2 s after touchdown if the graph is stuck in its jump state and skating.
  - **Animation:** start events try `JumpDirectionalStart` (FluidMotionNPC's NPC default), then the standing jump, then the existing OAR dodge-as-jump swap.
  - **FluidMotionNPC, if installed:** no jump while it moves the actor (`FMNPC_IsTraversing`), and it is told to leave the actor alone for the jump (`FMNPC_Claim`).
  - **Debug:** `Jump 0x..: side|other side|in place arc, N units, landing dz, T s`, `landed`, `aborted`, `no clear arc`.
  - **Removed:** the FJF velocity-launch helpers and the AE re-push loop.
## CombatAITree 0.4.0 (2026-10-22)

- **Movement preemption.** A running Advance/Circle/Fallback/Backoff node no longer plays out to its own end after the brain has changed its mind.
  - Mechanism: the four classes' Update (vtable slot 4) is hooked. A node of our branch stops once the brain no longer wants its kind. A vanilla movement node under our selector stops when the brain wants any move.
  - "Stop" means `CombatBehaviorThread::Ascend()`. That is what the classes' own Abort slot does (verified on SE, 1.6.1170 and 1.7.104), and CPR uses the same mechanism for Advance.
  - A node must have run 0.3 s before it can be stopped, so brain flip-flops can't make the feet stutter. Nodes in other trees are untouched.
- **Feedback.** The 60 s summary adds `running moves stopped: ours N, vanilla N`. The debug actor logs `Move 0x..: stopped running our|vanilla node K (brain wants move W)` (moves: 1 Advance, 2 Circle, 3 Fallback, 4 Backoff).
- **Docs.** The node slot labels in TREE_DESIGN §6 and COMBAT_AI_RE.md are corrected back to 2 Enter / 3 Exit / 4 Update / 5 Abort (from Thread::Update 46228). The custom leaf's state-reservation question is solved by delegating to Idle; the leaf itself is not built (TREE_DESIGN §6 says why).
## CombatAITree 0.3.2 (2026-10-22)

Run 5 (0.3.1): every in-range offense skip (102 of 102) was `line of sight blocked`, so the brain never got to score an attack. Feint dropped out of the decisions (0 picks; it had 198 in run 4).
- **Line of sight ignores bodies.** The `kLOS` ray hits actors as well as walls, and it reports only the closest hit, so the target's or the attacker's own body read as a wall at melee range. Now an actor hit (self, target or bystander) is not an obstruction; terrain and statics still are. The hit object is resolved with `TESHavokUtilities::FindCollidableRef` (ID 25466/26003, checked on SE, AE 1.6.1170 and 1.7.104).
- **Debug:** a blocked check logs `LOS 0x..: blocked at F of the ray by <name> 0x<ref>` ("geometry" when the hit has no reference).
## CombatAITree 0.3.1 (2026-10-22)

Run 4 (0.3.0, 3 bandits, ~4 min) ran clean, but the brain still never chose an attack: 0 Attack/PowerAttack candidates in 1508 decisions. All 115 bash intents were taken by the tree, and vanilla attacks still ran inside the tree.
- **Feint no longer resets the attack timer.** Feint is re-issued every decision (~0.1 s) while it wins, just as locomotion was before 0.2.3. It blocked 126 of the 146 in-range ticks that failed the 0.3 s rule. Its own feint timer is unchanged.
- **Offense skip reason (debug).** The other 288 in-range ticks passed the timer but still produced no attack, and none of the target-attacking, own-recovery or priority checks explains them on the logged state. Each skip now logs `Offense 0x..: in range (dist N) but no attack - <reason> <value>`, where the reason is line of sight blocked, target is attacking, last action too recent, own swing recovering, or priority too low. The prime suspect is the line-of-sight ray; that is unverified.
## CombatAITree 0.3.0 (2026-10-22)

- **Feedback summary**: once a minute (info level, only when something happened) the log shows, per kind, how many intents the brain posted, how many the trees took, and for attacks how many found attack data of the requested kind (light/power) in the Attack node - plus movement intent changes. Posted >> taken means the tree was busy; taken >> found means the actor has no attack data of that kind or not enough stamina.

## CombatAITree 0.2.4 (2026-10-22)

- **Circle on the brain's side.** Strafe and Flanking now pass their direction (left/right of the line to the target) with the Circle intent, and the Circle node's angle-getter call (SE 46720+0x299 / AE 47916+0x22B, byte- and target-checked) keeps vanilla's angle size but sets its sign. Without a side, vanilla's random side is kept. Sign convention (positive = left) comes from Fenix-NG and is not verified in game: debug line `Circle ...: vanilla angle a -> b (brain side +1/-1)`.

## CombatAITree 0.2.3 (2026-10-22)

- **Fix: the brain never attacked.** Offense has an anti-spam gate (no attack within 0.3 s of the last executed action), and every executed action reset that timer - including locomotion, which the brain re-issues every ~0.1 s while moving. Once movement ran through the tree (0.2.x), the timer stayed at ~0 and offense was never evaluated: 0 Attack/PowerAttack candidates in 1398 decisions in the 0.2.2 test. Advancing, Strafe, Flanking, Retreat and Backoff no longer reset it.
- `scripts/package_release.ps1`: one-command release packaging (DLL + assets, tree validation, zip).

## CombatAITree 0.2.2 (2026-10-22)

- **Removed the "our defense" attack veto** (and `DefenseVetoSeconds`). It stopped vanilla attacks for 0.6 s after every brain dodge, block, parry, retreat, backoff or strafe, which in the 0.2.1 test vetoed 299 of 440 attack checks and left bandits passive once movement actually ran. Its job is covered now: vanilla branches already yield while a brain bash/block intent is pending, and the Block/Bash nodes are vanilla's own. The pacing veto ("waiting its turn") stays.
- Debug log: `tree runs move N` when one of our movement branches is entered (separates our movement from vanilla's in the log).

## CombatAITree 0.2.1 (2026-10-22)

- **Fix: game hang when a hostile NPC entered combat (0.2.0).** The engine's AddChildren helper (SE 46305 / AE 47549) climbs each node's parent chain and adds the topmost ancestor. 0.2.0 passed vanilla's Movement Selector while it was still attached, so the tree root itself became a child and the CloseMovement tree contained a cycle. The selector is now detached first, and SetChildren refuses attached nodes.

## CombatAITree 0.2.0 (2026-10-22)

- **Movement through the tree, CPR no longer required.** The vanilla CloseMovement tree is kept, and our branches are spliced in above its Movement Selector (hook on the builder's SetRoot call, SE 46731+0x512 / AE 47928+0xE9C, byte- and target-checked; an unexpected tree shape leaves vanilla untouched).
- Brain movement decisions post a movement intent that the brain refreshes every decision: Advancing = Advance node, Strafe/Flanking = Circle, Retreat = Fallback, Backoff = Backoff. Each branch runs only while the brain keeps asking; otherwise vanilla's movement logic runs as before.
- If CPR is installed, its graph variables are still set and shape the same vanilla nodes.
- No backing off mid-swing: an active Fallback/Backoff intent is dropped while the actor attacks (with the CPR variables).
- Known limit: a running movement node finishes before the next decision takes effect (no preemption yet).

## CombatAITree 0.1.1 (2026-10-22)

- Fix (0.1.0 regression): posting an intent that is still pending now fails, like the old raw events did when the graph refused them. In 0.1.0 Feint re-posted a bash every brain tick (29 posts, 4 taken in the first test log).
- A bash the brain asked for while a Block node was holding now fires as the block ends (post-block counter-bash branch) instead of expiring.

## CombatAITree 0.1.0 (2026-10-22) - fork of EnhancedCombatAI 1.10.2

First version of the in-tree actuator (design: `TREE_DESIGN.md`).

- **Our own Melee and Block behavior trees** replace the vanilla tree builders (SE 48151/46640,
  AE 49182/47843). They are built from vanilla node classes (node layer ported from FenixCombatAI-NG),
  so vanilla's Attack, Special Attack, Block, Block Attack and Bash nodes execute every action:
  real `TESActionData` actions, proper attack data, vanilla's Action-resource arbitration.
- **Intent board** (`Intents.h`): the brain's Attack / PowerAttack / Bash / Parry / TimedBlock
  decisions post a one-shot intent instead of sending raw `attackStart`, `attackPowerStartInPlace`,
  `bashStart` or `blockStart` animation events. The intent branch sits first in its tree.
- **Attack kind**: an intent-driven attack only picks attack data of the requested kind (light or
  power) through the existing attack-chance hook, and always passes the vanilla roll.
- **Vanilla behaviour without intents**: every other branch rolls vanilla's own chance function,
  rescaled because our trees re-read every 0.05 s instead of 0.25 s (same per-second rate).
- Renamed: `CombatAITree.dll`, `CombatAITree.log`, `CombatAITree.ini`, API plugin name `CombatAITree`.
  Do not install together with EnhancedCombatAI or FenixCombatAI (both replace the same trees).
- Unchanged for now (still the old actuator): movement (CPR graph variables), dodge, jump, feint,
  forward dodge and sprint attack (`attackStartSprint`).

## Recent Updates

### Action Types
- ✅ Added `ActionType::Jump` - Jump evasion for ranged attacks (uses dodge system with OAR)
- ✅ Added `ActionType::Backoff` - Back away when target casting/drawing bow
- ✅ Added `ActionType::Advancing` - Close distance when too far (CPR advancing)

### Decision System
- ✅ Added tie-breaking system for decisions with same priority
- ✅ All decisions now have intensity values
- ✅ Improved decision selection with context awareness (health, distance, intensity)

### Performance
- ✅ Added per-actor processing throttling (each actor has own timer)
- ✅ Optimized BFCO state management (removed redundant resets)
- ✅ Timer cleanup integrated with actor cleanup

### Detection
- ✅ Added bow drawing detection (`isDrawingBow` in TargetState)
- ✅ Enhanced casting detection

### Integration
- ✅ Improved CPR integration (disabling conflicting behaviors before enabling new ones)
- ✅ Optimized BFCO integration (explicit flag setting instead of full reset)
- ✅ Jump action uses dodge system with animation replacement support (OAR)

### Bug Fixes
- ✅ Fixed Windows macro conflicts (min/max/clamp) in Config.cpp, ActorStateObserver.cpp, CombatStyleEnhancer.cpp
- ✅ Fixed CombatStyleEnhancer to use correct TESCombatStyle fields (powerAttackBlockingMult, meleeScoreMult)
- ✅ Fixed decision matrix logic (floating-point comparisons, priority handling)
- ✅ Fixed mutually exclusive action handling in CombatStyleEnhancer (if-else chains)
- ✅ Fixed debug logging to show all possible decisions
- ✅ Fixed std::format typo (was std:format)
- ✅ Fixed CombatDirector::Update() call from hooks (timer-based approach)

### Combat Style Enhancements
- ✅ Added support for Advancing action in AggressiveStyle
- ✅ Added support for Backoff action in DefensiveStyle
- ✅ Improved action priority adjustments based on combat style multipliers

### Code Quality
- ✅ Improved intensity value calculations for all action types
- ✅ Better distance-based intensity for Backoff and Advancing
- ✅ Enhanced CPR behavior conflict resolution
