# Changelog

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
