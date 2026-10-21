# Configuration Guide

## Overview
EnhancedCombatAI uses an INI configuration file to customize all aspects of the combat AI system. The configuration file is located at:

**`Data/SKSE/Plugins/EnhancedCombatAI.ini`**

## Installation

1. Copy `EnhancedCombatAI.ini.template` to your Skyrim `Data/SKSE/Plugins/` directory
2. Rename it to `EnhancedCombatAI.ini` (remove `.template` extension)
3. Edit the file with your preferred settings
4. Restart Skyrim or reload the plugin

## Configuration Sections

### [General]
General plugin settings.

- **EnablePlugin** (bool, default: `true`)
  - Master switch to enable/disable the entire plugin
  - If `false`, plugin loads but does nothing

- **EnableDebugLog** (bool, default: `false`)
  - Enable detailed debug logging
  - Useful for troubleshooting

- **ProcessingInterval** (float, default: `0.1`)
  - How often to process actors (in seconds)
  - Lower = more responsive but more CPU usage
  - Recommended: `0.1` (100ms)

### [Humanizer]
Combat-entry reaction delay, skill rolls and cooldowns.

- **BaseReactionDelayMs** (`200`), **ReactionVarianceMs** (`100`), **ReactionDelayReductionPerLevelMs** (`2`), **MinReactionDelayMs** (`50`)
  - One-time delay before an NPC that just entered combat acts (shrinks with level, plus random variance)

- **EnableSkillRolls** (bool, default: `true`)
  - Deliberate actions (dodge, parry, timed block, bash, jump, attacks, feint) roll once per opportunity:
    one roll covers a whole incoming swing; other openings re-roll every **SkillRerollSeconds** (`1.0`)
  - A failed roll lets that opportunity pass: the NPC eats that hit or misses that opening
  - Movement never rolls; projectile sidesteps use `[ProjectileAwareness] DodgeChance`

- **SkillBase** (`0.55`), **SkillPerLevel** (`0.008`), **SkillMin** (`0.2`), **SkillMax** (`0.95`)
  - Skill = SkillBase + SkillPerLevel x (level - 1), clamped; level 1 = 55%, level 50 = 94%

- **BashCooldownSeconds** (`3.0`), **SprintAttackCooldownSeconds** (`3.0`), **DodgeCooldownSeconds** (`2.0`), **JumpCooldownSeconds** (`3.0`)

- **EnableFatigue** (bool, default: `true`), **FatigueOnsetSeconds** (`45`), **FatigueFullSeconds** (`180`), **FatigueStaminaThreshold** (`0.25`), **FatigueSkillPenalty** (`0.5`)
  - Long fights and low stamina raise the failure chance (1 - skill), up to x(1 + penalty)

- **EnablePanic** (bool, default: `true`), **PanicHealthThreshold** (`0.3`), **PanicSkillPenalty** (`0.75`)
  - Below the health threshold the failure chance rises (not for retreating), up to x(1 + penalty)

### [DodgeSystem]
Settings for TK Dodge integration.

- **DodgeStaminaCost** (float, default: `10.0`)
  - Stamina cost per dodge
  - NPCs must have this much stamina to dodge

- **IFrameDuration** (float, default: `0.3`)
  - Invulnerability frame duration (seconds)
  - How long NPCs are invulnerable during dodge

- **EnableStepDodge** (bool, default: `false`)
  - Use step dodge variant
  - Requires animation graph support

- **EnableDodgeAttackCancel** (bool, default: `true`)
  - Allow dodging during attacks (attack cancel)
  - More responsive but may feel unrealistic

- **DodgeEventForward / DodgeEventBack / DodgeEventLeft / DodgeEventRight** (string, defaults: `TKDodgeForward` / `TKDodgeBack` / `TKDodgeLeft` / `TKDodgeRight`)
  - Animation event sent for each dodge direction. Name another framework's events to dodge with it,
    e.g. The Ultimate Dodge Mod: `ForwardRollStart`, `BackwardRollStart`, `Left_RollStart`, `Right_RollStart`
  - An empty value keeps the default

- **DodgeEventFallback** (string, default: empty)
  - Sent when the graph rejects the direction's event (e.g. `RollStart` for TUDM). Empty = no second try

- **MinDodgeStamina** (float, default: `0`)
  - Stamina an NPC must have to start a dodge, without being charged it. The larger of this and
    `DodgeStaminaCost` is checked. Use it when the dodge framework charges its own cost (e.g. TUDM)

- **ForwardDodgeMaxGap** (float, default: `200`)
  - Forward dodge (TK Dodge) only closes the last stretch: target at most this far beyond attack reach, and only when the target is open (casting, drawing a bow, recovering from a swing, staggered, fleeing or facing away)

- **ForwardDodgeCounterWindowSeconds** (float, default: `1.5`)
  - Also dodge in when the target is recovering from a swing the NPC evaded within this window

### [DecisionMatrix]
Settings for tactical decision making.

- **InterruptMaxDistance** (float, default: `150.0`)
  - Maximum distance for bash interrupt (game units)
  - Target must be power attacking within this distance

- **EnableEvasionDodge** (bool, default: `true`)
  - Enable evasion dodge when target is blocking
  - If `false`, NPCs won't dodge when target blocks

- **StaminaThreshold** (float, default: `0.2`)
  - Stamina threshold for survival retreat (0.0 to 1.0)
  - NPCs retreat when stamina falls below this percentage
  - `0.2` = 20% stamina

- **HealthThreshold** (float, default: `0.3`)
  - Health threshold for survival retreat (0.0 to 1.0)
  - NPCs retreat when health falls below this percentage
  - `0.3` = 30% health

- **EnableSurvivalRetreat** (bool, default: `true`)
  - Enable survival retreat behavior
  - If `false`, NPCs won't retreat when low on stamina/health

- **EnableSprintAttack** (bool, default: `true`)
  - Enable NPC sprint (gap-closer) attacks
  - If `false`, NPCs advance/run toward the target instead of sprint attacking

### [Performance]
Performance optimization settings.

- **CleanupInterval** (float, default: `5.0`)
  - How often to clean up invalid actor references (seconds)
  - Higher = less frequent cleanup

- **MaxActorsPerFrame** (int, default: `0`)
  - Maximum actors to process per frame
  - `0` = unlimited (let processing interval handle throttling)
  - Use if experiencing performance issues

### [TargetSwitching]
NPCs turn on whoever just hit them, or switch to a nearly dead enemy nearby.

- **EnableTargetSwitching** (bool, default: `true`)
- **SwitchCooldownSeconds** (float, default: `5.0`) - at most one switch roll per actor per cooldown
- **RetaliateWindowSeconds** (`2.0`), **RetaliateChance** (`0.5`), **RetaliateMaxDistance** (`600`)
- **FinisherHealthThreshold** (`0.25`), **FinisherMaxDistance** (`500`), **FinisherChance** (`0.35`)

### [ArmorStyle]
Body armor weight shapes behavior (clothing/none: unchanged).

- **EnableArmorStyle** (bool, default: `true`)
- **LightEvasionBonus** (`0.15`) - light armor: more dodge/strafe/jump/flank
- **HeavyEvasionPenalty** (`0.2`) - heavy armor: fewer dodge/strafe/jump
- **HeavyGuardBonus** (`0.15`) - heavy armor: more bash/timed block

### [Teamwork]
- **EnableFollowerPincer** (bool, default: `true`) - followers use the player as their flanking partner
- **FollowerPincerBonus** (`0.3`) - extra flanking priority when the target is engaged with the player

### [CombatPacing]
Caps how many NPCs engage one target at once (Wait Your Turn style).

- **EnableCombatPacing** (bool, default: `true`), **MaxSimultaneousAttackers** (`2`)
- **SlotWindowMinSeconds** / **SlotWindowMaxSeconds** (`2` / `4`) - how long a slot is held before rotating
- **PaceTargetPlayerOnly** (`true`) - only pace fights against the player
- **EngageMargin** (`150`) - an NPC attacking (any source) or within reach + this needs a slot
- **WaitDistanceMargin** (`120`) - NPCs without a slot circle at reach + this (back off if inside reach); their vanilla attacks are vetoed, swings in progress are never cancelled. SCAR movesets pick their own attacks, so raise this if SCAR NPCs still lunge from the ring
- **RetaliateMinSeconds** / **RetaliateMaxSeconds** (`1` / `3`) - a waiting NPC hit by its target may fight back this long

### [ProjectileAwareness]
NPCs sidestep (TK Dodge) projectiles on course to hit them: arrows, bolts, spell projectiles and flame
streams, read from the engine's own combat threat list. Runes and other stationary threats are not
handled yet. The usual dodge cooldown and stamina cost still apply; DodgeChance replaces the skill roll.

- **EnableProjectileDodge** (bool, default: `true`)
- **ReactMinSeconds** (`0.2`), **ReactMaxSeconds** (`0.9`) - react only when impact is this far away in time
- **DodgeChance** (`0.6`) - rolled once per projectile
- **HitMargin** (`40`) - extra radius counted as "on course to hit"
- **DodgePriority** (`2.2`) - decision priority (melee evasion is ~1.5-2.2)

### [VanillaAttacks]
Shapes the attacks the vanilla melee AI picks by multiplying each candidate attack's chance
(the engine weights candidates by it, then rolls the picked one; 0 = never). The engine still
plays the moveset's own attack data. Not used on SkyrimVR; SCAR-driven NPCs keep SCAR's choice.

- **EnableVanillaAttackShaping** (bool, default: `true`)
- **OpeningPowerMult** (`2.0`), **OpeningNormalMult** (`1.5`) - target staggered, recovering or casting
- **BlockedPowerMult** (`1.5`), **BlockedNormalMult** (`0.25`) - target blocking and facing the attacker

### Presets
The in-game menu (SKSE Menu Framework) has Default, Vanilla+, Souls-like and Chaos
preset buttons. A preset overwrites every tuning section except General, Performance
and ModIntegrations, starting from the defaults. Press "Save to INI" to keep it.

### [ModIntegrations] - SCAR
Auto-detected via `SCAR.dll`. Movesets patched for SCAR raise the `SCAR_ComboStart` animation
event; on those NPCs SCAR picks normal/power attacks and this mod keeps defense, movement and
sprint attacks. Unpatched movesets keep this mod's offense.

- **EnableSCARIntegration** (bool, default: `true`)
- **SCARComboWindowSeconds** (`20`) - a `SCAR_ComboStart` this recent marks the moveset as SCAR-driven
- **SCARStallSeconds** (`3`) - a SCAR NPC that wants to attack this long without attacking gets this mod's attack (badly patched movesets)

## Example Configuration

```ini
[General]
EnablePlugin = true
EnableDebugLog = false
ProcessingInterval = 0.1

[Humanizer]
BaseReactionDelayMs = 200.0
ReactionVarianceMs = 100.0
SkillBase = 0.55
SkillPerLevel = 0.008
BashCooldownSeconds = 3.0
DodgeCooldownSeconds = 2.0

[DodgeSystem]
DodgeStaminaCost = 10.0
IFrameDuration = 0.3
EnableStepDodge = false
EnableDodgeAttackCancel = true

[DecisionMatrix]
InterruptMaxDistance = 150.0
EnableEvasionDodge = true
StaminaThreshold = 0.2
HealthThreshold = 0.3
EnableSurvivalRetreat = true

[Performance]
CleanupInterval = 5.0
MaxActorsPerFrame = 0
```

## Tips

1. **Start with defaults** - The default values are balanced for most gameplay
2. **Adjust gradually** - Make small changes and test
3. **Use debug logging** - Enable `EnableDebugLog` to see what's happening
4. **Performance tuning** - If experiencing lag, increase `ProcessingInterval` or set `MaxActorsPerFrame`
5. **Difficulty scaling** - Adjust `SkillBase` and `SkillPerLevel` to make AI easier/harder

## Troubleshooting

**Plugin not working:**
- Check `EnablePlugin = true` in `[General]` section
- Check log file for errors

**NPCs too reactive/not reactive enough:**
- Adjust `SkillBase` / `SkillPerLevel` (how often they catch an opening or defend a swing)
- `BaseReactionDelayMs` only delays the first action after entering combat

**Too many/few dodges:**
- Adjust `DodgeCooldownSeconds`
- Check `EnableEvasionDodge` is `true`

**Performance issues:**
- Increase `ProcessingInterval`
- Set `MaxActorsPerFrame` to a lower value
