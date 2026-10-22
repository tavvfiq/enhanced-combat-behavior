# Changelog

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
