# Changelog

## CombatAITree 0.29.0 (2026-10-26)

- **Boss detection (step 1 of boss handling, logging only; no behavior change yet).** An NPC is a boss when its placed reference carries vanilla's "Boss" location ref type (Skyrim.esm `Boss`, 0x000130F7, on dungeon bosses such as bandit chiefs) or its name is in `[Boss] BossNames` (default `Bandit Chief, Draugr Deathlord`; deathlords also appear as plain elites without the marker). Log: `Boss 0x..: Bandit Chief (boss marker, name) level 28 vs player 20` once per boss; selecting an NPC in the console adds `Boss check 0x..: BOSS|not a boss (boss marker X, listed name Y, level Z)`.
- Debug: the roll self-test from 0.28.1 still logs at load.

## CombatAITree 0.28.1 (2026-10-26)

- **Debug: the forward-dodge roll shows its value.** `ForwardDodge 0x..: ... -> chance 0.35, rolled 0.62 (bucket N): walk`; one bucket is one 1.5 s opening, so repeated lines with the same bucket are one roll.
- **Roll self-test at load:** `Roll self-test passed ( 0.35 -> 0.35x; 0.50 -> 0.50x; over 20000 keys each)`. Runs 53-54 had no forward-dodge "go"; an offline run of the same roll gave 0.346-0.356 at 0.35 over 5 random salts, so that was luck (or fewer distinct openings than lines), not a skewed roll.

## CombatAITree 0.28.0 (2026-10-26)

- **NPCs walk in instead of circling from afar.** Flanking now only happens near the target (within 2.5x the attack range) and only when the target isn't facing that NPC; when it is, the NPC is the target's focus and fights instead. Before, flanking won from up to 1500 units over walking in (run 53: Flanking 950 of 1633 actions, Attack 6). NPCs waiting for an attack slot still circle (pacing), unchanged.

## CombatAITree 0.27.0 (2026-10-26)

- **Personality reaches the careful and patient choices.** Caution scales backing off after its own swing and on the target's swing, the per-swing parry / timed block roll and the close dodge, gently (x0.6 .. x1.4) so a Foolhardy bandit still blocks and backs off, just less. Patience gates feints: one roll per 2 s, 40% (impatient) to 100% (patient).
- **More spread between NPCs of one template.** Confidence and Aggression now weigh more than the combat style's attack/defend balance (all-offense for vanilla bandits), and the fixed per-NPC variation is +-0.15 (was +-0.1). Run 52's bandits read risk 0.84 and 0.90 from identical data.
- **Combo follow-ups reach a bit farther:** up to 1.3x the attack range, since a combo step lunges forward (run 52: 81 windows skipped as out of reach, the Berserker's median at 160 against a range of about 144).

## CombatAITree 0.26.0 (2026-10-26)

- **Forward dodge is a risky move now, not a habit.** "Target facing away" no longer counts as an opening (in 1 vs many, every NPC you weren't facing saw one; run 51: all 5 forward dodges of the selected NPCs). Real openings only: casting, drawing a bow, recovering from a swing, knocked down, fleeing, or the counter after evading its swing; never into a raised guard facing it. Then a roll once per opening (`ForwardDodgeChance` 0.25, x2 for a counter), its own 8 s cooldown, and half stamina. Debug: `ForwardDodge 0x..: opening (target busy) gap X, risk R -> chance C: go|walk`.
- **NPC personality.** From data every NPC already has: Confidence (Cowardly .. Foolhardy) and Aggression (Unaggressive .. Frenzied) actor values, its combat style's offensive/defensive balance and avoid-threat chance, plus a small fixed per-NPC variation. It gives risk, caution and patience (0..1). This version uses risk: forward dodge (x0.25 .. x1.75, halved when outnumbered or hurt unless reckless), sprint attacks (one roll per 2 s, 40% .. 100%) and trading blows (x0.25 .. x1.75). Caution and patience are logged for the next step. Debug: `Personality 0x..: confidence 2, aggression 1, style offense 0.50, avoid threat 0.30 -> risk 0.45, caution 0.42, patience 0.60`. INI: `[DecisionMatrix] EnablePersonality`.

## CombatAITree 0.25.1 (2026-10-26)

- **The combo follow-up now actually chains.** It sends vanilla's attack event (`attackStart`, or `attackPowerStartInPlace` for power) the way vanilla's Attack node does. In run 51 (BFCO) every combo that chained followed an `attackStart` inside the window, while 0.25.0's bare attack action (it played `BFCOAttackstart_0`) never chained on its own. The MCO NPC idles 0.25.0 looked for don't exist in this setup and are dropped.
- **A back-off the brain then drops no longer blocks the follow-up.** The follow-up check now runs after the "back off this swing?" roll: in run 51, 84 open windows were skipped as "defending" on Backoff decisions that the roll turned into "keep swinging".

## CombatAITree 0.25.0 (2026-10-26)

- **Our own combo follow-up for MCO and BFCO movesets, for use without SCAR.** The movesets mark the combo windows with annotation events (MCO_WinOpen / MCO_WinClose, MCO_PowerWinOpen / MCO_PowerWinClose, BFCO_NextWinStart / BFCO_NextPowerWinStart / BFCO_DIY_EndLoop). Inside an open window the NPC rolls once to start the next attack (normal 70%, power 30% when its window is open and stamina allows); the moveset's behavior already knows which attack comes next. No follow-up while it waits for an attack slot, defends, or the target is out of reach. It plays MCO's NPC attack idles (`ADXP_NPCNormalAttack` / `ADXP_NPCPowerAttack`) the way SCAR does, or the bare attack action when they are missing. Off while SCAR.dll is loaded. INI: `[ModIntegrations] EnableComboFollowUp`, `ComboFollowUpChance`, `ComboPowerChance`.
- Log at load: `Combo follow-up: MCO/BFCO window events hooked; MCO NPC idles ... found (event X)`. Debug (selected NPC): `Combo 0x..: MCO_WinOpen`, `window open (normal true, power false) -> normal follow-up`, `normal follow-up started` / `FAILED (graph refused)`.

## CombatAITree 0.24.0 (2026-10-26)

- **SCAR attacks now follow the brain (SCAR stays installed and still picks the move).** While an NPC waits for an attack slot, holds a block, or the brain is dodging, blocking, backing off or retreating, SCAR can no longer start an attack or continue a combo for it. Before, pacing only reached vanilla attacks; SCAR's bypassed it. Debug: `SCAR gate 0x..: attack start refused (waiting its turn)` / `combo refused (...)`. Log at load: `SCAR gate installed`. Only when SCAR.dll is loaded and the SCAR integration setting is on.
- **The brain closes to SCAR's real attack range.** For an NPC on a SCAR clip, the brain's attack reach is the farthest range of the clip's likely actions (50%+ chance; all actions if none) instead of the weapon's reach, so it no longer stands where SCAR won't swing, or walks in closer than needed. Debug: the `Misc: WeaponReach=` line shows the SCAR reach.
- Trampoline raised to 1024 bytes (30 call hooks).

## CombatAITree 0.23.2 (2026-10-26)

- **Archers and mages at range are left out of attack pacing.** A bow draw counted as attacking, so a ranged NPC took one of the 3 attack slots on you (or was held back waiting for one) from across the camp (run 49: an archer at 1400+ units 11 times). Now only NPCs within melee reach + 150 units take part, whatever their weapon.
- **A shot hold no longer slips.** The hold factor now lasts 1 s instead of 0.4 s between brain ticks; run 49's archer drew once in a 0.62 s gap between ticks. Vanilla does re-roll its ranged chance before every shot, so there is no chained shot to stop.

## CombatAITree 0.23.1 (2026-10-26)

- **Casters and archers no longer hold fire for long behind an ally.** A hold lasts 3 s at most, then the NPC shoots or casts as vanilla would for 1.5 s before the next hold (run 48: a sorcerer held 15 s). Debug: `shot chance x1.0 (held 3 s, shooting anyway)`.
- **Stepping aside works more often.** When the step-aside fails on one side (no path, or outside the view cone), the next one goes the other way (`step aside (other side: the last one failed)`). An ally in the line of fire inside the max distance now comes before "out of sight, close in", because the ally's body can be what blocks the sight.

## CombatAITree 0.23.0 (2026-10-26)

- **Casters hold fire with an ally in the way.** A "Hold Cast" branch now sits in front of vanilla's cast selector in the Magic tree (after the dual block): while an ally stands in the line to the target, the NPC doesn't start a cast, and a running stream (concentration) spell stops. This covers mages and hybrids with a spell in either hand. Single casts already charging are left alone. Debug log: `Ranged 0x..: concentration cast stopped`.
- **SCAR action data reader, read-only (step 1 of letting the brain drive SCAR/MCO/BFCO movesets).** It reads the `SCAR_ActionData{...}` annotations on the NPC's active animation clip, with SCAR's format and range rules (SCAR 2.0.1, MIT, credit max-su-2019). Nothing is triggered yet and SCAR keeps doing its job. For the selected NPC, when the brain wants to attack (or the NPC is in its combo window) the log lists the actions the clip offers and which would fit right now: `SCAR 0x..: N actions on the active clip (t X), best fit <event>: ...`. The parser checks itself against SCAR's documented examples on first use (`SCAR data reader self-test passed`).

## CombatAITree 0.22.2 (2026-10-26)

- **Line of fire checks every ally**, up to 8 the combat scan sees (was the closest one only). The archer steps away from whichever ally is in the way.
- **Debug: Fallback start audit.** Fallback moves often failed to start (run 45: 23 of 29). For the selected NPC the MoveNode line now says which start check refused: `[start checks: not animation-driven X, far enough Y, straight path Z]` (1 passed, 0 refused, -1 not reached).
- **Debug: attack cooldown / stamina probe.** About one in nine attacks the tree took never reached vanilla's attack checks. For each candidate attack that gets past its per-attack cooldown, the selected NPC logs `AttackCheck 0x..: candidate <event> past its cooldown, stamina cost X (has Y)`, with `REFUSED` when the stamina is short. A refused attack with no such line was stopped by the cooldown.

## CombatAITree 0.22.1 (2026-10-26)

Run 47 (0.22.0): archers shot about three times as often (31 draws vs 10), the redirect ran without problems, and a raised guard cut the shot chance on 41 ticks. But the x2 "opening" fired on 266 ticks, mostly because you faced another bandit.

- **"Looking away" is no longer an opening for archers.** The x2 shot chance now needs you recovering from a swing, casting or drawing, staggered, or fleeing.

## CombatAITree 0.22.0 (2026-10-26)

- **Archers time their shots.** Vanilla decides when to draw with a random roll; the brain now scales that roll each moment (the Ranged tree's chance pointer is redirected, SE 32398+0xA3 / AE 33149+0x111):
  - an ally between the archer and its target: no shot (×0);
  - the target's guard raised and facing the archer: much less likely (×0.3), it waits for the guard to drop;
  - an opening (target recovering, casting or drawing, staggered, fleeing, or looking away): twice as likely;
  - otherwise vanilla.
  Debug log: `Ranged 0x..: shot chance xN`. Load log: `Tree: Ranged attack chance redirected`.
- **Clearing the line of fire.** An archer or mage with an ally in the way (the closest ally, within 55 units of the line to the target) steps sideways, away from the ally. Debug log: `Ranged 0x..: ally in the line of fire -> step aside`.
- Spell timing for mages is not shaped yet (the Magic tree's cast rolls are built differently); they get the positioning and line-of-fire step only.

## CombatAITree 0.21.2 (2026-10-26)

Run 46 (0.21.1): a Bandit Sorcerer (spell in hand) fell back from you and actually moved (76-81 units per fallback), then switched to its dagger. The contact cutoff worked ("left to vanilla" 198 times). But the archer's fallbacks all failed at once: 7 of 9 came while its bow was drawn.

- **Archers finish the shot, then fall back.** No fall-back request while the NPC is drawing, aiming or casting (any attack state): vanilla's Fallback node won't start then. Debug log: `Ranged 0x..: melee target at X, finishing its shot first`.

## CombatAITree 0.21.1 (2026-10-26)

Run 45 (0.21.0): ranged positioning works. Bow-drawn archers fell back from you (Retreat was their top action), closed in from beyond 2000 units or out of sight, and kept shooting (25 draws). But a walking archer can't outrun a running player: it backed off 30-54 units while you closed from 133 to 88.

- **No falling back in contact.** Below 150 units an archer or mage stops backing off and is left to vanilla, which switches to its melee weapon (the archers did, to their daggers). Falling back now only applies between 150 and `RangedKeepAwayDistance` (450). Debug log: `Ranged 0x..: melee target in contact at X -> left to vanilla`.

## CombatAITree 0.21.0 (2026-10-26)

- **Archers and mages take the brain's moves.** Vanilla runs a different movement tree for NPCs whose max range is over 1024 (bows, crossbows, staffs, most spells): RangedMovement, not CloseMovement. Our movement branches were only in CloseMovement, so every move the brain picked for them did nothing. The RangedMovement builder's SetRoot is now hooked too (SE 48476+0x495, AE 49468+0x957), with the same Advance/Circle/Fallback/Backoff branches in front of vanilla's.
- **Ranged positioning.** For an NPC fighting with a bow, crossbow, staff or spell in the right hand, the melee rules (advance to reach, attack, flank, feint, bash) are off. Instead:
  - a melee attacker closer than `RangedKeepAwayDistance` (450): it falls back, more urgently while that attacker swings at it;
  - target farther than `RangedMaxDistance` (2000) or out of sight: it closes in;
  - in between: vanilla shoots, casts, strafes and keeps its own optimal range.
  Survival retreats, dodges and back-off still apply. `EnableRangedPositioning` turns it off. INI and menu. Debug log: `Ranged 0x..: melee target at X -> fall back` / `-> close in to Y`.
- **Fix: staffs read as crossbows.** The weapon-type table had staff (8) and crossbow (9) swapped. Inherited from combat-ai-ng.

## CombatAITree 0.20.2 (2026-10-26)

Run 43 (0.20.1, group fight) was still passive: 32 attacks in 1232 executed actions. In reach, 106 ticks were pacing holds, and on 157 no attack was considered (38 "not facing the target" while circling, 26 trade roll lost, 14 too soon after the last action). Counters were posted but vanilla refused them during the block-hit reaction (hit cone or range check), and only 1 of 11 swung.

- **A timed block doesn't delay the next attack.** It reset the 0.3 s "last action" wait like a swing does.
- **Counter window 0.2-1.0 s after the blocked blow** (was 0-0.6 s), past the block-hit reaction.
- **Attacks allowed up to 75 degrees off the facing** (was 50). Vanilla's own cone passed attacks at up to 82 degrees, and the tighter limit kept circling NPCs from ever attacking.
- **Flanking capped at 1.4 within 1.5x attack range**, not only inside it, so NPCs close in instead of circling just out of reach.
- **Three attackers at once** (`MaxSimultaneousAttackers = 3`, was 2). If your INI was saved from the menu, change it there.

## CombatAITree 0.20.1 (2026-10-26)

Run 42 (0.20.0, group fight): flanking, now running outdoors, took over. 1662 of ~1960 executed actions were Flanking and only 48 were attacks, Flanking even winning inside attack range (292 vs 46). The counter worked when it got the chance (counters scored 5.3-6.7 and swung) but often lost to flanking or to your next swing.

- **Flanking gets an NPC beside you, then it attacks.** Once it is at your side or back, it no longer flanks (the "keep the position" rules that kept it circling are gone; the attack already gets a flanking bonus there). Inside attack range flanking is capped at 1.4, so an attack with an opening wins.
- **A counter goes in even if you chain your next swing.** The counter window skips the trade-blows roll.

## CombatAITree 0.20.0 (2026-10-26)

- **Counter attacks without a mod.** When an NPC blocks one of your melee blows (shield, weapon or dual block), a 0.6 s counter window opens while you recover. In it the brain strongly prefers a power attack, or a normal attack when it lacks the power-attack stamina. Before, the counter only existed with EldenCounter (its graph variable still counts too), and its bonus was being thrown away anyway (fixed in 0.19.0). Debug log: `Counter 0x..: blocked a hit, counter window open 0.6 s`.

## CombatAITree 0.19.1 (2026-10-26)

The three audit choices from 0.19.0:

- **Flanking runs outdoors.** Its ally check now uses the ally's distance (known and within 3000 units) instead of the ally's world coordinates, which outdoors were always "too far". This logic has barely run before, so group fights change: allies work around you more. Watch whether they still press the attack enough.
- **Feints only against a guard.** A feint (a bash) needs the target blocking, or blocking often lately. It used to fire on a target that was just facing the NPC.
- **No attack bonus for a blocking target.** A normal or sprint attack into a block bounces off; the +0.3 for "target blocking" is gone. Power attacks keep their guard-break bonus, and bash and feint keep theirs.

## CombatAITree 0.19.0 (2026-10-26)

Decision matrix and pacing audit (code read, no run). Bugs fixed, most of them inherited from combat-ai-ng:

- **"Target just finished attacking" read the wind-up as an opening.** The attack scoring treated 0.1-0.5 s after the target's last attack as its recovery window, but that timer restarts when a swing starts, so it pointed at the swing itself. It now means "out of the swing and idle for under 0.4 s".
- **The guard counter bonus was thrown away.** In the power-attack choice, a good opening *set* the power bonus to 0.3 instead of adding 0.3, wiping the guard counter's +2.0 and the weapon and matchup bonuses.
- **Power attacks were chosen at a loss.** Any "prefer power" rule picked the power attack even when its total bonus was negative, so it could win with a lower score than a normal attack. A power attack now needs a positive bonus.
- **A swing at someone else stopped the approach.** Advancing and the sprint attack still waited out any swing of the target's, also one aimed at another NPC (the 0.14.0 fix covered strafe, back-off and attacks). They now check the aim too.
- **Being outnumbered cancelled the pursuit of a fleeing target** (the outnumbered priority overwrote the pursuit boost). It no longer does.
- **Pacing: the same NPC kept the attack slot.** When a slot's time ran out, the holder, already in reach and asking every tick, usually took it straight back, so a waiting NPC might never get a turn. A holder whose slot just ran out now sits out 1.5 s while someone else is waiting.

Found but not changed (behavior choices, see STATE.md): flanking never runs outdoors (an "is the ally position valid" check uses world coordinates, which are always too large there); feints fire on a target that is merely facing the NPC; attacking a blocking target gets a bonus.

## CombatAITree 0.18.7 (2026-10-26)

- **Fix (0.18.6 regression): a dual block now lasts until your blow actually lands.** 0.18.6 ended the hold when the attacker went into "recovery", but between combo swings that flag is already set before the next blow comes. In run 40 that ended 30 blocks about 0.15 s in, and only 5 of your 73 hits on the Bandit Chief were blocked. The hold now ends on the hit itself (blocked or not), or when you stop attacking, or after 1.2 s. Debug log: `DualBlock 0x..: the blow landed, ending the hold`.

## CombatAITree 0.18.6 (2026-10-26)

Run 39 (0.18.5): the fixes worked. Your hits blocked on the sword+spell Bandit Chief went from 2 to 7 of 57, defend checks from 25 to 55 window ticks, and 17 timed blocks ran. But 7 of the 17 ended before your blow, which landed 0.56-0.78 s after the block went up. Your swings came in two speeds (start to hit 0.12-0.50 s for 33 of them, 0.63-1.11 s for 24), so a single predicted hit time can't fit both.

- **A dual block holds until the swing is over**, not until the predicted hit plus 0.25 s: it ends once your blow lands (you go into recovery) or you stop attacking, at most 1.2 s. The NPC can't attack while it holds, so this trades some of its offense for blocks that last. Debug log: `DualBlock 0x..: swing over (hit landed|target stopped attacking), ending the hold`.

## CombatAITree 0.18.5 (2026-10-26)

Run 38: of your 57 hits on the sword+spell Bandit Chief, 20 landed with no block check at all. Three causes, each fixed:

- **Contact range counts.** Parry and timed block refused targets closer than 50 units, and you were hitting from 42-47 (8 of the 20). The minimum is now 0 (`ParryMinDistance`, `TimedBlockMinDistance`). If your INI was saved from the menu it may still say 50.
- **Hit timing is the median of the last 9 hits.** The 0.16.1 outlier filter backfired: three early long samples (1.0-1.2 s, a combo swing whose start was missed) set the average to 0.94 s, and from then on the real 0.12-0.45 s hits were thrown out as outliers. The prediction stuck near 0.86 s and the block window was never reached (6 of the 20). A median shrugs off wrong samples either way.
- **No bash or feint requests without the Block tree.** The chief posted 8 bashes that could never be taken, and the 0.11.1 guard against untaken requests then switched off all its defense checks for 10 s at a time, the dual block included (likely the other 6). NPCs without the Block tree now only get the direct timed block.

## CombatAITree 0.18.4 (2026-10-26)

The 0.18.3 dump shows the vanilla Magic tree: Magic Context > "Casting Type Selector" > one case per casting type ("Cast Immediate Repeat", "Cast Concentration Repeat") > that type's spell selector (prepare dual cast, cast, idle). Those "Repeat" cases are value nodes, not loop nodes, which is why both earlier splices found nothing.

- **Spell hands get the dual block** (third try). Our branch now goes in front of each casting type's spell selector. The loop that re-runs the tree is the equipment selector above it, so every pass reads our branch first. The load log says `Tree: Magic tree extended (dual block in 2 casting types)`.
- **Fix: the cast stop covered only dual casts.** The two classes 0.18.2 hooked are the dual-cast nodes; a normal cast is a separate class. All four (single and dual, immediate and concentration) now stop for a wanted dual block. They share the same exit, which interrupts the cast. The dump is removed.

## CombatAITree 0.18.3 (2026-10-26)

Run 36 (0.18.2): the Melee-tree dual block caught 3 of 5 of your hits (`blocked=true`) on a dual-wielding Bandit Chief. The Magic tree splice found no loop node at all (`Tree: Magic tree has no Repeat node`), so spell hands still don't dual block.

- **Debug: the vanilla Magic tree's layout is logged once at load** (`Tree dump:` lines: node names, node class offsets, child counts), to see where its casting loops really are. No behavior change.

## CombatAITree 0.18.2 (2026-10-26)

- **A cast stops for the block.** When the brain wants a dual block while the NPC's spell hand is casting, the running cast node stops the way our movement preemption stops a movement node. Vanilla then runs that node's own exit, which interrupts the cast (MagicCaster::InterruptCast, checked in SE 1.5.97, AE 1.6.1170 and 1.7.104), and the Magic tree's loop picks up the block. Before, the block waited for the cast to finish and could expire first. Debug log: `DualBlock 0x..: immediate|concentration cast stopped for the block`. Only NPCs the brain asks to dual block are affected.

## CombatAITree 0.18.1 (2026-10-26)

Run 35 (0.18.0): the dual block works through the Melee tree. A dual-wielding Bandit Chief blocked 3 times; the first caught your hit (`blocked=true`), and you didn't land one during the other two.

- **Spell hands get the dual block now.** The Magic tree was left vanilla ("unexpected shape"): vanilla's Magic tree has no loop right under its root, it has one per casting branch further down. Our branch now goes inside every loop it finds (the log says how many).
- **A held block stays up while the NPC moves.** The old executor drops any block before a movement action, and a Feint cut one 0.15 s early. It now leaves a held dual block alone, and the brain doesn't feint (a bash) while the block is held.

## CombatAITree 0.18.0 (2026-10-25)

- **NPCs with a weapon plus a spell, torch or second weapon can timed-block.** Vanilla never gives them the Block tree, so nothing ever asked them to block. Run 34's probe showed their animations do block when told the way Simple Dual Wield Block does it for the player: set the "wants to block" flag, send `blockStart`. Both tries that weren't cut short blocked your hit. Now a timed block for these NPCs runs a new tree node (an Idle node with our own start and end steps). It is the first branch in the Melee tree (dual wield) and in the Magic tree (a spell in the left hand, via a new hook on the vanilla Magic builder). The node holds the block until just after your predicted hit (0.3-1.0 s), and while it runs that hand can't swing or cast. The brain also posts no attacks or bashes while the block is held, which is what broke one probe block. Same per-swing defend roll and priority as the shield timed block. No parry for these NPCs (that needs the Block tree's bash).
- Pure casters (spells in both hands, or a spell and an empty hand) don't get it: run 34 tried 47 times and nothing blocked, they have nothing to block with.
- If the tree thread goes away mid-block (combat ends, NPC dies), the brain releases the block itself.
- The 0.17.1 probe is gone. Debug log: `DualBlock 0x..: blockStart accepted` and `released, blockStop accepted`; load log: `Tree: Magic SetRoot hooked` and, once a magic user fights, `Tree: Magic tree extended`.

## CombatAITree 0.17.1 (2026-10-25)

- **Debug probe: direct block for dual-wielders and spell users.** For the NPC selected in the console, when your swing aimed at it enters the timed-block window and its equipment can't run the Block tree (a spell, torch or second weapon in the left hand), the plugin tries Simple Dual Wield Block's method: set the "wants to block" flag, send `blockStart`, hold 0.6 s, then clear it and send `blockStop`. The log shows `DualBlock 0x..: left <hand>, right <hand> -> blockStart accepted|rejected`, then whether the NPC reads as blocking after 0.2 s. It decides whether a custom block node for these NPCs can work. Only the selected NPC, only with the debug log on.

## CombatAITree 0.17.0 (2026-10-25)

- **Daggers can block and bash** (`AllowDaggerBlock = true`, `[DecisionMatrix]`, applies at game start). Vanilla's weapon-block check refuses daggers and fists with one bit mask; the plugin changes that mask so only fists are refused. Dagger users then run the block/bash behavior like other one-handed weapons: vanilla blocks and bashes, plus our bash, parry and timed block. This changes vanilla for every dagger user. Patched in SE 1.5.97, AE 1.6.1170 and 1.7.104 after checking the original byte; if another mod already changed it, nothing is written and the log says so. The load log shows `Dagger block: NPCs with a dagger can block and bash`.

## CombatAITree 0.16.2 (2026-10-25)

- **Weapon blocking is back for swords, axes, maces and two-handed weapons.** 0.16.1 allowed parry and timed block only with a shield, which was too strict. Disassembly (SE 1.5.97) shows how vanilla decides: the Block tree runs for a shield, or for a "one-handed block" combat item that every humanoid NPC has. That item is equipped when the right hand holds a melee weapon that is not a dagger and not fists. So daggers and fists can't block, which is why run 33's dagger bandit never blocked even in vanilla, and bows, staffs and crossbows can't either. A spell or torch in the left hand takes the slot. Parry and timed block now follow the same rule.

## CombatAITree 0.16.1 (2026-10-25)

Run 33 (0.16.0): timed block works. 4 timed blocks raised the shield 0.23-0.30 s before your hit, and all 4 hits were blocked (5 of your 29 hits were blocked all fight). The learned timing settled near 0.47 s.

- **Parry and timed block only for NPCs with a shield.** The timed block also accepted a weapon in the right hand, but only shield users run the Block tree, so a dagger bandit posted 3 blocks that could never be taken.
- **Learned hit timing ignores outliers.** From the 3rd hit, a sample under half or over double the average is skipped (logged as `ignored`). Run 33 had samples of 0.12 s and 1.30 s among ~0.45 s hits, and one 1.30 s moved the average to 0.68 s.
- Parry ran 0 times: it needs the 35 bash stamina, and the bandits were below that on 33 of 37 window ticks, so they timed-blocked instead. That is the game's bash cost, unchanged.

## CombatAITree 0.16.0 (2026-10-25)

Run 32 (0.15.0): the one timed block that ran worked (block up 0.06 s after the request, your hit 0.29 s later, blocked). But there were only 2 timed blocks and no parries in 3.5 minutes.

- **Hit timing is learned from real hits.** The brain predicted your blow at 60% of a fixed 0.37 s swing, 0.22 s after the swing starts. Measured over 41 of your hits it lands at a median 0.46 s, so the prediction was about 0.24 s early and a parry's bash would have come out too soon. Each attacker's swing-start-to-hit time (light and power separately) is now averaged from its melee hits, blocked or not. Until 2 hits are seen it uses the old estimate plus 0.24 s. The debug log shows `HitTiming 0x.. -> 0x..: swing start to light hit X s (average Y s over N)`.
- **Parry and timed block take their turn in the per-swing reactions.** They scored 1.36-1.75 and lost to back-off, strafe and attacks. Now, when a swing aimed at the NPC is inside the parry or timed-block window, one roll per swing (`DefendOnSwingChance`, default 0.5, INI and menu) decides whether it defends. A won roll outranks back-off, strafe and trade-blow attacks but not a won close dodge. Parry when it has the bash stamina, else timed block. The skill roll still applies. Order per swing: dodge roll, defend roll, then back-off and trade blows. Debug: `Defend 0x..: swing in the parry window (hit in X s) -> parry|no (roll)`.

## CombatAITree 0.15.0 (2026-10-25)

- **Parry and timed block are on by default and need no mod.** A parry is a bash timed to land when your swing hits; a timed block is a block started just before it. Both are vanilla actions run through the Block tree. EldenParry and Simple Timed Block are now optional: EldenParry adds the parry effect and success feedback, and Simple Timed Block's window spell is applied only when it is loaded. Before, timed block needed `SimpleTimedBlock.esp` or never ran, and both were off in the shipped INI. This is also the first time the brain asks for a block at all (0 block requests in every earlier run).
- **Only against swings aimed at the NPC**, like the dodge and strafe: no parry or block against your swing at its neighbour.
- **A timed block works at low stamina.** It no longer waits for the bash stamina cost (a block costs none); a parry still does.
- Needs a shield: only shield users run the Block tree (0.14.0 finding).
- If your INI was saved from the in-game menu, it may still say `EnableParry = false` / `EnableTimedBlock = false`; turn them on in the menu (Parry / Timed Block tabs).

## CombatAITree 0.14.1 (2026-10-25)

- **Fix (0.13.1 regression): a forward dodge that can't go is an advance again, not a dodge.** CombatDirector already turned a failed forward dodge into an Advance and recorded it as one. 0.13.1 added a second fallback inside the executor that reported the advance as a successful forward dodge. In run 31, 43 of 47 "forward dodges" were really advances, and each reset the dodge and evasion timers. Removed; the 0.13.1 note that run 30's NPC "stood still" was wrong, CombatDirector's fallback was already advancing it.
- Run 31 (0.13.2) confirms the dodge fixes: 10 of 12 dodges moved the way their event said. The other 2 were followed straight away by more movement inside the measuring window. The path check worked twice in real use: `TKDodgeLeft blocked (obstacle)` then dodged right; `TKDodgeForward blocked (actor at shoulder)` then dodged back.

## CombatAITree 0.14.0 (2026-10-25)

In a fight against several NPCs, everyone treated your swing as aimed at them. Three more reactions now need the attack aimed at this NPC, using the same check the dodge has used since 0.11.0:

- **Strafe:** a swing at someone else no longer makes the NPC beside it sidestep.
- **Back off:** a swing, cast or bow draw aimed at someone else is no reason to back off.
- **Attack:** a swing aimed at someone else no longer stops this NPC attacking ("target is attacking"). It is an opening, so NPCs flanking you should press harder while you fight their friend.

## CombatAITree 0.13.2 (2026-10-25)

- **Dodge path check is body-wide.** Two more rays run 22 units left and right of the centre line, so a pillar, tree or door frame that would clip a shoulder blocks that direction too. The debug line says `blocked (obstacle at shoulder)` or `(actor at shoulder)` for those. Still at waist height, so very low rocks and logs are not seen.

## CombatAITree 0.13.1 (2026-10-25)

Run 30 confirmed the 0.13.0 left/right fix: all 5 accepted dodges moved the way their event said (right 2, back 1, forward 2).

- **A won close-dodge roll now always goes first.** Trade-blows and punish attacks scored 3.05-4.70 and beat the 3.0 it had, so 0.12.0's "the dodge roll comes first" didn't hold. A won roll now outranks every option; a lost roll leaves back-off and trade blows as before.
- **Forward dodge no longer blocks on its own target.** The path ray hit the target the dodge was closing on, so 17 forward dodges failed with "blocked (actor)". The target is now ignored, other things in the way still count. A forward dodge that can't go now advances instead of leaving the NPC standing.
- **Honest close-dodge log.** The `close swing ... -> dodge` line was written before the stamina check, so some "dodge" lines had no dodge behind them. It now says `dodge`, `no dodge (roll)` or `no dodge (stamina)`.

## CombatAITree 0.13.0 (2026-10-25)

- **Dodge left and right were swapped.** Skyrim's heading turns clockwise but the angle maths counted counter-clockwise, so a dodge meant for the NPC's right sent `TKDodgeLeft`, and the reverse. An arrow sidestep went across the arrow's line instead of away from it, and the ledge check looked at the side the NPC didn't move to. Found in the code; the next log confirms it (see below).
- **Dodges check their path.** Before dodging, a ray at waist height checks the next ~220 units for a wall, object or actor, alongside the existing ledge check. A blocked direction is skipped for the next option. If none is clear, there is no dodge and the brain picks something else.
- **Attack dodges pick a side.** Was always the attacker's left. Now a random side, the other side if that one is blocked, then straight back. Arrow sidesteps try the side away from the arrow, then the other side. The forward dodge only goes forward.
- **Debug lines.** `Dodge 0x..: event TKDodgeX accepted (facing x,y); TKDodgeY blocked (obstacle)` and `Dodge 0x..: no clear direction (...)`.

## CombatAITree 0.12.0 (2026-10-25)

- **Close dodge.** NPCs can now dodge a melee swing (light or power) that is aimed at them from within the attacker's weapon reach (plus 30 units). Before, the attack dodge only worked 250-400 units out, where a light swing can't reach, so nothing dodged up close. It is rolled once per swing (`CloseDodgeChance`, default 0.3, INI and menu). A won roll goes first; a lost roll leaves the back-off and trade-blows rolls to decide that swing as before. It needs TK Dodge, the dodge stamina, and the NPC not mid-attack; the skill roll still applies, so roughly 1 swing in 5 gets dodged at default skill. Swings at someone else, and your recovery after a hit, don't count. The debug log shows `Evasion 0x..: close swing at X (reach Y) -> dodge|no dodge (roll)`.
- The target's own weapon reach is now gathered for this; the NPC's own reach is the fallback.

## CombatAITree 0.11.1 (2026-10-25)

- **No more feint spam when bash can't run.** In run 29 the bow/dagger bandit, holding its dagger, feinted 75 times. A feint is a bash, and its 11 bash requests were never taken (its Block tree wasn't running). Once 3 block/bash requests in a row expire untaken, the NPC stops asking for bash or feint for 10 s; a request the tree takes clears it. Feint also needs the bash stamina cost now (35 in this setup).
- Run 29 confirms 0.11.0: no projectile sidesteps from allies (no projectile lines at all), no attack dodges (you made no shots or power attacks at range), bash fired 5 of 6 taken (was 1 of 12) with `Bash stamina cost 35.0`.

## CombatAITree 0.11.0 (2026-10-25)

- **NPCs dodge only what is aimed at them.** In run 28, 7 of 9 dodges were sidesteps from an allied bandit archer's arrows aimed at you, because the NPC stood on the arrow's line. Projectile sidesteps now count only shots from someone hostile to that NPC. The other 2 dodges came while you swung 254-340 units away, probably at someone else. The attack dodge now needs the target's attack aimed at this NPC (the same aim line the jump uses, plus your bow crosshair). It also needs an attack that can reach that far: a shot, a power attack or a sprint attack. The old "dodge when the target is blocking" trigger is gone.
- **No bash without the stamina for it.** Bash waits until the NPC has the game's base bash stamina cost (`fStaminaBashBase`, logged once as `Bash stamina cost`). In run 28 vanilla refused 10 of 11 bashes at 0-8% stamina.
- **No attack for 1 s after a weapon switch.** Run 28's bow/dagger bandit had 6 attacks refused within 0.7 s of drawing its dagger.
- Unchanged: the forward dodge (a gap-closer toward an open target), and the strafe fallback when the target attacks.

## CombatAITree 0.10.0 (2026-10-25)

Run 27's probe showed where the lost attacks go: of 53 attack intents the tree took, 31 fired; 11 were turned down before vanilla's checks, 8 failed its range/path check and 3 its hit cone.

- **A failed attack no longer costs 0.3 s.** The "last action too recent" gate now starts when the NPC's attack or bash animation actually starts (its graph accepts `attackStart*`, `attackPowerStart*`, `bashStart` or `bashPowerStart`), not when the brain asks. A request that vanilla turns down leaves the gate open, so the brain asks again on its next evaluation (about 0.1 s). This also covers vanilla's own attacks and feint bashes.
- **No plain attack while sprinting.** 4 of the 11 early refusals came at sprint speed. The sprint attack is unchanged.
- **Faces the target before attacking.** No attack request with the target more than 50 degrees off its facing (vanilla's hit cone failed 3 at 57-100 degrees); it turns first.

## CombatAITree 0.9.9 (2026-10-25)

- **Debug probe: vanilla attack start checks.** Run 26 (0.9.8) showed that every attack event sent was accepted; the lost attacks never sent one. Of 65 taken attack intents, 12 picked an attack and then sent nothing, and 8 never got as far as picking one. Vanilla's StartAttack fires only after a hit-cone check and a range/path check, and now each is logged for the selected NPC: `AttackCheck 0x..: hit cone pass|FAIL (target at X deg, dist Y, arg Z)` and `AttackCheck 0x..: range/path check pass|FAIL`. A taken intent with no AttackCheck line was turned down before either check (per-attack cooldown, stamina cost or no candidates). No behavior change.
- Run 26 also confirms TK Dodge: 14 of 14 dodge events accepted, the NPC moves 150-340 units in about 0.7 s.

## CombatAITree 0.9.8 (2026-10-25)

- **Debug probe: animation events.** With the debug log on, every attack, bash and block animation event the game sends the selected NPC is logged as `AnimEvent 0x..: <event> accepted|rejected`. In run 25 the tree took every attack intent, but only 14 of 33 started a swing within 1 s (bashes 6 of 25), and the log had no signal past the hand-off. This shows whether the attack node sends its event and whether the graph takes it. No behavior change.

## CombatAITree 0.9.7 (2026-10-25)

- **Cornered NPCs fight.** A low-health (or losing-the-race) retreat ends after 8 s in total without the NPC's health rising; then it fights instead of running. In run 25 a bandit at 24% retreated for 60 s, up to 1820 units away, while you healed from 92% to 100%. Healing 5% from its lowest point (a potion, a spell) resets the count. The debug Race line shows `Retreated=`.
- Run 25 confirms 0.9.6: no back-to-back bash re-posts.

## CombatAITree 0.9.6 (2026-10-25)

- **Bash waits 0.3 s after the NPC's last action, like attacks.** In run 24 a bash was re-posted every tick (0.11 s) while the NPC's own bash was still winding up, up to 5 times in a row for one blocked hit. Each re-post reset the action timer, which kept the next attack waiting.
- Run 24 confirms 0.9.5: timers advance 0.109 s per evaluation (wall clock 0.11 s). In-reach attacks blocked by "last action too recent" fell from 185 to 49; attacks rose from 17 to 43 and bashes from 7 to 25 in a similar fight.

## CombatAITree 0.9.5 (2026-10-25)

- **NPC timers run at real speed.** Each NPC is evaluated about every 0.1 s, but its timers advanced by one frame's time (~0.012 s) per evaluation, so they ran about 9x slow. The 0.3 s "last action too recent" gate really lasted ~2.7 s: it blocked 185 in-reach attacks in run 23, and during your recovery NPCs strafed (133 ticks) instead of hitting. Every cooldown, duration and the swing-elapsed time behind parry/block timing was off by the same factor. The timers now advance by the time since that NPC was last evaluated.
- Run 23 confirms the 0.9.4 recovery fix: target recovery seen on 168 of 1014 ticks (was 1 of 1066).

## CombatAITree 0.9.4 (2026-10-24)

Data audit: fixes to what the brain sees, each found in code and checked against run 22.

- **Your recovery is an opening again.** The target counted as recovering only in follow-through, which happened on 1 of 1066 ticks. Every "punish the recovery" rule was dead, and NPCs treated your post-hit moments as a live swing: they backed off, held their attack and didn't advance. Recovery is now everything after your blow lands (hit, combo window, follow-through). NPCs no longer back off from a swing that already landed.
- **Each combo swing is its own swing.** Swing starts were detected only from idle, and a combo never returns to idle. So your second and third swings shared the first one's rolls, and parry/timed-block timing ran from the first swing.
- **Attack timing uses attack speed.** The swing-duration estimate divided by `SpeedMult`, which is movement speed. It now uses `WeaponSpeedMult`.
- **Power-attack flag** only while the target is attacking (the attack data can outlive the swing).
- **Swing ids stay unique across target switches**, so an old roll can't come back.
- **A blocked hit is neither a hit nor a miss.** 5 of 17 in-reach "misses" in run 22 were blocked hits. They inflated the miss rate, which cuts attack priority by up to 0.4.
## CombatAITree 0.9.3 (2026-10-24)

- **Archers no longer feint.** A feint starts a bash, and our Block tree never runs a bash for a bow or crossbow user. In run 21 one archer posted 23 bashes that were never taken (the summary's `block/bash never taken` line) and stood still meanwhile.

## CombatAITree 0.9.2 (2026-10-24)

- **Damage race.** Each NPC now knows how hard it hits its target and how hard the target hits it: the average share of max health per hit, over the whole fight, measured from both sides' hits (two hits are needed per side). From that it estimates how many more hits each side needs.
  - **Winning** (it kills you in fewer hits than you need): it keeps pressing. Attack priority +`RacePressBonus` (0.4), no low-health retreat, and both back-off chances halved. Glass cannons (mages, archers in light armor) usually put it here without any special rule.
  - **Losing** (`RaceRetreatHits` = 2 or fewer of your hits from death, and not winning): it retreats even above `HealthThreshold`, up to `RaceRetreatMaxHealth` (75%). This is the "it got hit hard" retreat.
  - No data yet: the old rules apply. Menu: Momentum tab. Debug line per tick: `Race: OurHit= TheirHit= ToKill= ToDie= Winning= Losing=`.
- **Trading blows.** While you swing (not a power attack), `TradeBlowsChance` (0.3) per swing lets it attack anyway instead of waiting, doubled while it's winning the race. Debug line: `trades blows with the target's swing`.
- **Fix: once-per-swing rolls were once per tick.** `attackStartTime` is the swing's elapsed time, not an id, so the Humanizer's defense skill roll and the back-off roll on your swings re-rolled every tick. A 35% back-off chance worked out near 90% over one swing. They now key on a swing counter. Expect fewer back-offs from your swings, and defense skill rolls that really fail sometimes.
## CombatAITree 0.9.1 (2026-10-24)

- **A failed back-off roll no longer turns into a sidestep.** In run 19, 177 of 273 failed rolls picked Strafe and 46 picked Retreat, so the NPC still disengaged, only sideways. A failed roll now takes the next-best option only when it is not a movement (bash, dodge, attack...). Otherwise it leaves the moment to vanilla, which blocks or swings. The debug line reads `- None instead` then.

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
