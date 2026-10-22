> **CombatAITree 0.1.0** - fork of EnhancedCombatAI that executes attacks, blocks and bashes through its own behavior trees (vanilla leaf nodes) instead of raw animation events. See `TREE_DESIGN.md` and `CHANGELOG.md`. Do not install alongside EnhancedCombatAI or FenixCombatAI.

# EnhancedCombatAI

A SKSE plugin that enhances Skyrim's combat AI to make NPCs more reactive and tactical, moving beyond probability-based combat styles.

## Features

### Core Combat AI
- **Tactical Decision Making**: NPCs evaluate combat situations and make intelligent decisions
- **Priority-Based Rules**: Interrupt > Evasion > Survival
- **Reactive Behavior**: NPCs respond to player actions (blocking, power attacks, etc.)
- **Organic Feel**: Level-scaled skill rolls (one per opportunity), a combat-entry reaction delay, and cooldowns

### Combat Actions
- **Bash Interrupts**: NPCs bash when target is power attacking within reach
- **Evasion**: NPCs dodge/strafe/jump when target is blocking or attacking
- **Retreat**: NPCs retreat when low on stamina or health
- **Backoff**: NPCs back away when target is casting magic or drawing bow
- **Advancing**: NPCs close distance when too far from target
- **Offensive Actions**: Normal attacks, power attacks, and sprint attacks
- **Combat Style Enhancement**: Each NPC type behaves uniquely based on their combat style

### Mod Integrations
- **Combat Pathing Revolution (CPR)**: Enhanced movement control via animation graph variables
- **Attack frameworks (MCO, BFCO, SCAR)**: Attacks start with the vanilla events, which every framework takes over from
- **TK Dodge RE**: NPC dodging system for evasion
- **Precision**: Accurate weapon reach calculations

## Requirements

### Required
- Skyrim Special Edition (1.6.640+) or Anniversary Edition
- SKSE64
- [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG)

### Optional (for enhanced features)
- **Combat Pathing Revolution (CPR)**: For advanced movement control (circling, fallback, advance)
- **TK Dodge RE**: For NPC dodging/evasion
- **Precision**: For accurate weapon reach calculations
- **SKSE Menu Framework**: For the in-game runtime configuration menu (edit settings live)

## Installation

1. Download the latest release
2. Extract to your Skyrim directory (where `SkyrimSE.exe` is located)
3. Ensure SKSE64 is installed and working
4. Copy `EnhancedCombatAI.ini` to `Data/SKSE/Plugins/` (or use the template)
5. Launch Skyrim via SKSE64

## Configuration

The plugin uses `EnhancedCombatAI.ini` for configuration. A template is provided (`EnhancedCombatAI.ini.template`).

### Runtime configuration (in-game)

If **SKSE Menu Framework** is installed, open the Mod Control Panel and select **Enhanced Combat AI → Config** to edit settings live without restarting:
- Decision-matrix toggles, distances, and scoring weights apply **instantly**.
- Humanizer values apply within a tick.
- **Save to INI** persists your changes; **Reload from INI** re-reads the file.

Without the framework the plugin still works normally — the menu simply doesn't appear.

### Scoring weights

The `[ScoringWeights]` section exposes the high-impact decision priorities (per-action base priorities plus the main offense modifiers), so you can rebalance which actions NPCs favor without recompiling.

### Key Settings

#### General
- `EnablePlugin` - Enable/disable the plugin
- `EnableDebugLog` - Enable debug logging

#### Humanizer
- `BaseReactionDelayMs` - Combat-entry reaction delay (milliseconds)
- `SkillBase` / `SkillPerLevel` - Chance to act on an opportunity, scaled by NPC level
- `BashCooldown` - Bash action cooldown (seconds)
- `DodgeCooldown` - Dodge action cooldown (seconds)

#### Dodge System
- `DodgeStaminaCost` - Stamina cost for dodging
- `DodgeIFrameDuration` - Invincibility frame duration
- `EnableStepDodge` - Enable step dodges
- `EnableDodgeAttackCancel` - Allow dodging to cancel attacks

#### Decision Matrix
- `InterruptReachMultiplier` - Reach multiplier for interrupt decisions
- `EnableSprintAttack` - Enable NPC sprint (gap-closer) attacks (fall back to advancing if disabled)
- `EvasionEnabled` - Enable evasion system
- `SurvivalHealthThreshold` - Health threshold for survival mode (%)
- `SurvivalStaminaThreshold` - Stamina threshold for survival mode (%)

#### Performance
- `ProcessInterval` - Processing interval (milliseconds)
- `CleanupInterval` - Cleanup interval (milliseconds)

See `CONFIGURATION.md` for full documentation.

## How It Works

### Architecture

```
Actor::Update Hook
    ↓
CombatDirector (Singleton)
    ↓
ActorStateObserver → Gathers combat state
    ↓
DecisionMatrix → Evaluates rules (Interrupt/Evasion/Survival)
    ↓
Humanizer → Applies skill rolls/cooldowns
    ↓
ActionExecutor → Executes actions (animations/movement)
    ↓
CPR/TK Dodge/SCAR → Mod integrations
```

### Decision Priority

1. **Interrupt (Priority 1)**
   - Trigger: Target power attacking + Distance < Weapon Reach
   - Action: Bash

2. **Evasion (Priority 1-2)**
   - Trigger: Target blocking/attacking + Actor idle
   - Actions: Dodge/Strafe/Jump (via TK Dodge or CPR)

3. **Backoff (Priority 2)**
   - Trigger: Target casting magic OR drawing bow
   - Action: Backoff (away from target)

4. **Survival (Priority 2)**
   - Trigger: Low stamina (< threshold) or Low health (< threshold)
   - Action: Retreat (via CPR fallback)

5. **Offense (Priority 1)**
   - Trigger: Within attack range
   - Actions: Attack, PowerAttack, SprintAttack, or Advancing (if too far)

### Combat Style Enhancement

NPCs are enhanced based on their combat style:
- **Dueling Style**: More bash, less retreat
- **Flanking Style**: More movement, prefers strafe
- **Aggressive Style**: Less defensive, more offensive
- **Defensive Style**: More cautious, earlier retreat
- **Magic Style**: Maintains distance
- **Ranged Style**: Prefers optimal range

### Advanced Features
- **Tie-Breaking System**: When multiple decisions have the same priority, the system uses intensity, health status, and distance to select the best action
- **Per-Actor Throttling**: Actors are processed at configurable intervals (default 100ms) to optimize performance
- **Jump Evasion**: NPCs leap sideways out of your aim when you draw a bow on them (planned arc, the graph's own jump animation; no OAR or Behavior Data Injector files needed)
- **Intensity-Based Actions**: All actions have intensity values for more nuanced behavior

## Mod Compatibility

### Compatible Mods
- **Combat Pathing Revolution (CPR)**: Fully integrated, auto-detected
- **TK Dodge RE**: Fully integrated, auto-detected
- **Precision**: Fully integrated, auto-detected
- **MCO / BFCO / SCAR movesets**: Compatible; attacks use the vanilla attack events

### How Integration Works
- Mods are auto-detected at runtime
- Plugin gracefully falls back if mods aren't installed
- No conflicts - works with or without optional mods

## Building

### Prerequisites
- C++20/23 compatible compiler (MSVC recommended)
- xmake build system
- CommonLibSSE-NG (included in `lib/` directory)

### Build Commands
```bash
# Build the plugin
xmake build

# Clean build
xmake clean
xmake build

# Build with debug symbols
xmake build -m debug
```

## Development

### Project Structure
```
src/
├── main.cpp                 - Plugin entry point
├── Hooks.h/cpp              - Actor::Update hook
├── CombatDirector.h/cpp     - Singleton manager
├── ActorStateObserver.h/cpp - State gathering
├── DecisionMatrix.h/cpp     - Tactical logic
├── ActionExecutor.h/cpp     - Action execution
├── Humanizer.h/cpp          - Organic feel
├── DodgeSystem.h/cpp         - TK Dodge integration
├── CombatStyleEnhancer.h/cpp - Style enhancement
├── PrecisionIntegration.h/cpp - Precision integration
└── Config.h/cpp             - Configuration system
```

### Code Style
- Follows CommonLibSSE-NG conventions
- PascalCase for classes/structs
- camelCase for variables/methods
- `m_` prefix for member variables
- See `role.md` for detailed guidelines

## Documentation

- `IMPLEMENTATION_NOTES.md` - Implementation details and notes
- `PLAN.md` - Original implementation plan
- `CHANGELOG.md` - Recent changes and updates
- `CPR_INTEGRATION.md` - CPR integration guide
- `DODGE_INTEGRATION.md` - TK Dodge integration guide
- `PRECISION_INTEGRATION.md` - Precision integration guide
- `COMBAT_STYLE_ENHANCEMENT.md` - Combat style system
- `CONFIGURATION.md` - Configuration documentation
- `PROJECTGAPCLOSE_INSIGHTS.md` - Insights from ProjectGapClose

## Credits

- Inspired by ProjectGapClose's approach to combat AI
- Uses CommonLibSSE-NG for Skyrim engine access
- Integrates with Combat Pathing Revolution, SCAR, TK Dodge RE, and Precision

## License

MIT License

## Contributing

[Add contribution guidelines here]

## Support

[Add support information here]
