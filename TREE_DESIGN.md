# In-tree combat AI: design (draft 1, 2026-10-22)

Goal: fork **combat-ai-ng** (D:\Modding\combat-ai-ng, MIT) into a new mod. Keep its decision code
(observers, `DecisionMatrix`, the level-scaled skill rolls + cooldowns that still live in class
`Humanizer`, config, menu). Replace its actuator (`ActionExecutor`: raw
animation events and graph variables) with **our own behavior trees**, built from vanilla node
classes the way **FenixCombatAI-NG** (D:\Modding\skyrim_mods\FenixCombatAI-NG) does. Vanilla
leaf nodes then execute every action.

Out of scope for now: SCAR/MCO/BFCO/CPR/Precision/TK Dodge compatibility, VR.
Targets: **SE 1.5.97** and **AE 1.7.104** (1.6.1170 for free, because Fenix-NG is validated there).

Labels: **C** confirmed (binary or runtime), **L** likely, **U** unverified.

## 1. Why in-tree

| Problem with combat-ai-ng today | In-tree answer |
|---|---|
| Raw `attackStart`/`blockStart`/`bashStart` skip `TESActionData` and attack-data choice (RE §4.5, run 2) | Vanilla Attack/Block/Bash leaves fire the real action with the chosen attack data **C** |
| No arbitration: the vanilla tree keeps firing, and Block exit sends `blockStop` over ours (`46635`/`46630`) **C** | Our tree **is** the tree. Vanilla's Action/Movement resources arbitrate **C** |
| `Speed`/`Direction`/`Input*` writes are overwritten (FluidMotionNPC hooks `ActorSpeedChannel` for exactly this reason) **L** | Movement leaves (Advance/Circle/Fallback/Backoff/Surround/Reposition) drive `CombatPath` **C** |
| Vanilla helpers need the tree-thread TLS (#11) **C** | Conditions run inside tree threads, so StartAttack and CombatStyle getters are usable |

What it costs: actions are limited to vanilla leaf classes until we write our own leaf class (§6).
It conflicts with any mod that replaces the same tree constructors (FenixCombatAI, the original
combat-ai-ng). A mod that patches the same leaf code also conflicts if we install movement patches (CPR).

## 2. Architecture

```mermaid
flowchart LR
  subgraph Brain["Brain (combat-ai-ng, Character::Update hook, kept)"]
    OBS[ActorStateObserver] --> DM[DecisionMatrix + skill rolls/cooldowns]
  end
  DM -->|write| BB[(Intent blackboard\nper actor, versioned)]
  subgraph Tree["Our trees (engine combat job, tree thread)"]
    MEL[Melee tree] & BLK[Block tree] & MOV[CloseMovement tree]
  end
  BB -->|read in conditions| MEL & BLK & MOV
  MEL --> ATK[vanilla Attack leaf\n+ attack-chance hook]
  BLK --> BL[vanilla Block / Bash leaves]
  MOV --> MV[vanilla Advance/Circle/Fallback/Backoff/Surround\n+ parameter getter hooks]
  ATK & BL & MV -->|Enter/Exit + anim events| FB[(Feedback per actor)]
  FB --> DM
```

- **Brain:** unchanged combat-ai-ng code. It stops calling `ActionExecutor::Execute` and writes an
  `Intent` instead.
- **Intent blackboard:** a `ThreadSafeMap<FormID, Intent>` (the class already exists in combat-ai-ng).
  It is written on the update thread and read by tree threads running in parallel combat jobs.
  Every intent has an `expires` time, so a stale intent falls back to "no opinion".
- **Feedback:** leaf Enter/Exit and animation events (`attackStop`, `HitFrame`, `blockStop`,
  `bashStop`) report what actually ran, so the brain stops guessing.

```cpp
struct Intent {                    // one per actor, replaced whole on every brain decision
    enum class Attack : uint8_t { None, Light, Power, Sprint } attack;
    enum class Defend : uint8_t { None, Block, TimedBlock, Bash } defend;
    enum class Move   : uint8_t { Vanilla, Advance, Circle, Fallback, Backoff, Surround, Hold } move;
    float circleAngleDeg, holdDistance, fallbackDistance;   // feeds the movement getter hooks
    float expires;                 // AI time; past it the trees behave as Intent{} (vanilla-like)
    uint32_t seq;                  // bumps each write; feedback echoes it
};
```

## 3. Mapping combat-ai-ng actions to tree nodes

| combat-ai-ng `ActionType` | Tree / node | Mechanism |
|---|---|---|
| Attack, PowerAttack | Melee: `Attack` leaf behind a selector gate | Gate condition = `intent.attack != None`. The attack-chance hook (SE 48139+0x2AE / AE 49170+0x28D → 49748/50675, combat-ai-ng already hooks it) returns 0 for attack data that doesn't match Light/Power **C** (site), **U** (forcing power reliably) |
| SprintAttack | Melee `Attack` + sprint-attack data | **U**: whether the Attack node picks sprint attack data while sprinting. Fenix-NG has a `PolicySprintAttackCanary`; read it first |
| Bash, Parry | Block tree: `Bash` leaf gated by `intent.defend == Bash` | Fenix-NG `Block_wantbash` pattern, runtime-proven on AE 1.6.1170 |
| TimedBlock | Block tree: `Block` leaf gated by `intent.defend ∈ {Block, TimedBlock}` | Block's own Exit sends `blockStop`; release = intent cleared. Idle weight 0.25 s, the vanilla value (Fenix found 1.0 too slow) |
| Advancing | CloseMovement: `Advance` | Situation gated by `intent.move == Advance` |
| Strafe | CloseMovement: `Circle` | Angle/radius through `getCircleAngle` (49721/50648) and the `setTwoRadiuses` hook (Fenix Section C) |
| Retreat | CloseMovement: `Fallback` | Distance and wait through `getFallbackDistance`/`getFallbackWaitTime` (49718/50645, 49719/50646) |
| Backoff | CloseMovement: `Backoff` | — |
| Flanking | CloseMovement: `Surround` | Nearest vanilla equivalent; **L** semantics |
| Dodge, ForwardDodge, Jump, Feint | **none in v0.1-0.2** | Needs our own leaf class (§6). Until then these actions are disabled |

`Intent::Move::Vanilla` keeps vanilla's own CloseMovement selection, so v0.1 can ship without the
movement tree.

## 4. Trees (as built in 0.1.0)

0.1.0 uses vanilla's own shape (FallbackSelector<NextChildSelector> with RandomNode children)
instead of Fenix-NG's ValueNode/ConditionalChildSelector shape. A RandomNode with a probability of
1 or 0 works as an intent gate, and with vanilla's chance function it is the stock branch. The
RandomNode factory (SE 46641 / AE 47845) builds `ConditionalNode<RandomBoolChance(f(Attacker, Target))>`,
RTTI-checked on 1.7.104, the same class as vanilla's "RandomNode - Attack".

```text
Melee : Melee Context(slot 0) > Repeat > NextSelector{
          RandomNode(intent attack: 1/0) > Attack
          RandomNode(vanilla 49749/50676, rescaled) > Special Attack
          RandomNode(vanilla 49747/50674, rescaled) > Attack
          Idle(0.05 s) }
Block : Block Context(slot 1) > Repeat > NextSelector{
          RandomNode(intent bash) > Bash
          RandomNode(intent block) > Block > RandomNode(vanilla bash 49755/50682) > Bash
          RandomNode(vanilla 49750/50677, rescaled) > Block Attack > RandomNode(vanilla bash) > Bash
          RandomNode(vanilla 49756/50683, rescaled) > Offensive Bash
          RandomNode(vanilla 49751/50678, rescaled) > Block > RandomNode(vanilla bash) > Bash
          Idle(0.05 s) }
```

- Vanilla branches return 0 while an intent is pending, so they never jump in front of the brain.
- The Idle hold is 0.05 s instead of vanilla's 0.25 s (otherwise a timed block could start up to 0.25 s late).
  To compensate, vanilla chances are rescaled as `1-(1-p)^(0.05/0.25)`, which keeps the per-second rate unchanged.
- Block Context's equip slot is 1, as in vanilla (Fenix-NG writes 0).
- Before allocating, the node pool is initialized the way vanilla builders do it (flag SE 514112 / AE 400190). Fenix-NG skips this step.
- In 0.1.0 the executor is unchanged for movement, dodge, jump, feint, forward dodge and sprint attack (the old actuator).
  Attack and power attack still send a raw `blockStop` when the actor is blocking (old `Execute()` preamble).
## 5. Port status of Fenix-NG's offsets (checked 2026-10-22, `re/fenixport.py`)

Each AE 1.6.1170 ID in Fenix-NG `src/Offsets.h` was compared with AE 1.7.104. Function bodies were
compared as normalized instructions, with call and rip targets mapped to Address Library IDs.

| Result | Items |
|---|---|
| **Identical** | Tree machinery: `ctorNonvirtual`, `nodeAllocInit`, `nameNode`, `initNodes`, `addFirstChildren`, `initChildrensParents`, every node factory, `Conditional2_new`, `RandomNode0_new`, `GetLocaleT`, `isOutOfRangeCtx`, `shouldFallbackToRangedCtx`, `CombatAIGetMe/Target`. **All three tree constructors** (Melee 49182, Block 47843, CloseMovement 47928). `AttackChanceCaller` (+0x28D is valid). `CircleUpdate`, getters, `AttackUpdate`, `AdvanceAct`. **All 24 vtables** (same RTTI class, same 12 slots). `nodePool` data |
| Patch sites still valid | `FallbackUpdate` (+0xF8/+0x114/+0x128/+0x138/+0x227/+0x242), `FallbackWaitHost` +0x12E, `AdvanceRadiiHost` +0x127: the same instructions sit at the same offsets |
| **ID changed in 1.7.104** | `isMovingFrom` AE 47326 does not exist in 1.7.104. The same code is ID **523984** (FallbackUpdate+0x11B now calls it). An AE-only `RelocationID` would crash at load: map it per build |
| Code changed (only called, not patched) | `nodeAllocator` (register allocation only), `isStaggered` (one extra instruction), `checkCollisions`, `getFallbackSpeed` (jump-table data), `FindStandardLocationBody` |

SE IDs come from Fenix's original SE work. We haven't re-verified them with our SE tools yet (to do before the first SE build).

## 6. Our own leaf class (0.4.0, not started)

Dodge, jump and feint need a leaf node that holds the Action resource and fires a `TESActionData` (or
a dodge event) from inside the tree thread. That means implementing the node vtable ourselves.

**Slot map (resolved 2026-10-22, L→C for the parts below):** CPR's header (adopted by Fenix-NG) is right,
and our RE doc's "2 Enter / 3 Exit / 4 Update / 5 Abort" labels are wrong.

| Slot | Meaning | Evidence |
|---|---|---|
| 0 | destroy(bool free) | CPR |
| 1 | get_name | CPR; Fenix-NG: some classes return a class literal or NULL, not `name` |
| 2 | act(control) - enter/run | CPR; Idle slot 2 = 32430 |
| 3 | pop(control) - resumed after a child | CPR |
| 4 | on_childfailed(control) | CPR; Fenix-NG's slot-4 hook on the leaf Attack node logged 0 calls in a full fight |
| 5 | on_interrupted(control) | Idle slot 5 (529262) tail-jumps to `Ascend` (SE 46229) |
| 6 / 7 | SaveGame / LoadGame | CPR; ContextFlee slot 7 (47647) is the restore path |
| 8 | bool validate | Idle slot 8 tail-jumps to `CombatBehaviorAction::Validate` (529913) |
| 9 | ? (returns a BSFixedString*) | CPR, unknown |

Plan: derive our leaf from Idle's behaviour. Copy Idle's vtable at runtime, replace slot 2 (act: fire
the action, then behave like Idle with a short hold) and slot 1 (name), keep save/load/validate.
Open: the per-node state block in the control arena (`size_120`): how a node reserves it (Idle reserves
room for its timer). This needs a runtime probe before a release.
## 7. Roadmap

| Version | Content | Pass criteria (runtime, AE 1.7.104) |
|---|---|---|
| 0.1.0 | Fork. Intent blackboard. Melee + Block trees. Attack-chance hook driven by intent. `ActionExecutor` attack/block/bash paths removed; movement left vanilla | Marked bandit attacks, power attacks, blocks and bashes only when the brain says so (probe-verified); no crash for 10 min; save/load in combat OK |
| 0.2.0 | **Done (build):** our branches spliced above the vanilla Movement Selector via a hook on its SetRoot call; Advance/Circle/Fallback/Backoff; Flanking = Circle; Surround and parameter getters deferred | Strafe/retreat/advance visibly follow the intent; no movement-tree freeze |
| 0.2.x | Movement preemption (vanilla DynamicSelector RE) + parameter getters (circle side/radius, fallback distance) | brain changes take effect within ~0.25 s |
| 0.3.0 | Feedback channel to the brain (leaf Enter/Exit, anim events) | The brain stops reissuing actions that already run |
| 0.4.0 | Own leaf class: dodge / jump / feint | after §6 is resolved |

## 8. Open items before coding 0.1.0

1. Read Fenix-NG `Conditions.cpp` (attack gate, block gate, attack chance) and
   `PolicyAttackLifecycle.cpp` / `PolicySprintAttackCanary.cpp`, and keep what applies to an intent-driven gate.
2. Check that forcing Light vs Power through the attack-chance hook always yields an attack and never leaves an empty attack
   context. Fenix-NG's SCAR crash came from a fully-declined candidate set; the same risk exists in vanilla. **U**
3. Verify Fenix's SE IDs with our SE tools (`fenixport.py` dump for SE plus a name/RTTI check).
4. Thread model: confirm where `Character::Update` runs relative to the combat jobs (main thread
   or job). This decides whether the blackboard needs only atomics or a lock. **U**
5. Name and folder for the fork.

## 9. Movement preemption (0.2.x, designed, not built)

Problem: a running movement node (Circle, Fallback, Advance) finishes before the next brain decision
takes effect, because our intent selector is a FallbackSelector inside the CloseMovement tree. It only
re-reads when its current child returns.

**Vanilla already preempts one level up.** `Root - Movement` (SE 48431) is
`Acquire Movement Resource > Repeat > DynamicSelector<ConditionalChildSelector>(0.25 s)` with ValueNode
children (Essential Down, Flight, Flee, Hide, ReturnToCombatArea, ExitWater, Search, Ranged, Flanking,
CloseMovement links). It re-evaluates every 0.25 s and switches branches. Two ways to use it:

1. **Our branches as extra children of that DynamicSelector**, before the CloseMovement link, using
   the same ValueNode pattern. Blocker: Advance/Circle/Fallback read the **CloseMovement context**.
   So each branch needs `Context(CloseMovement) > leaf` under its ValueNode, and Fenix-NG documents that
   a ValueNode forwards its logic node's act without changing `current_state`, so a Descend inside it
   indexes the **wrapper's** child array. Vanilla stores the logic node as both `+0x28` and child[0]
   ("ValueNode - Flight Link" > "Flight Link"). A Context logic node that Descends(0) would then land on
   itself: a probable infinite loop (game hang, like 0.2.0). Needs a runtime probe of ValueNode::act
   with a non-leaf logic node first.
2. **Our own DynamicSelector inside CloseMovement** (class `NodeObject1<DynamicSelector<ConditionalChildSelector>, float>`,
   same layout family as Idle: float period at +0x28). Its children would be ValueNode10(WantX) > leaf for
   our four moves, and ValueNode1(1) wrapping vanilla's Movement Selector. That last wrapper hits the same
   "children belong to the wrapper" rule: vanilla's selector children would have to move to the
   wrapper's array.

Both need one probe session (log `cur_node`/`current_state` around ValueNode::act on a test tree) before
they can ship safely. Until then a running movement node holds for its own duration (vanilla feel).