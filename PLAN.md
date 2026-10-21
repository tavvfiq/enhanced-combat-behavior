> **CombatAITree fork:** the current plan is `TREE_DESIGN.md` (roadmap section 7). The text below is the original EnhancedCombatAI plan, kept for reference.

# Combat AI Enhancement - Implementation Plan

## Overview
This document outlines the step-by-step implementation plan for enhancing Skyrim's combat AI to make NPCs feel more reactive and tactical rather than probability-based.

## Architecture

### Core Components
1. **CombatDirector** (Singleton) - Central manager for all combat AI logic
2. **ActorStateObserver** - Gathers state data for actors and targets
3. **DecisionMatrix** - Evaluates state and makes tactical decisions
4. **ActionExecutor** - Executes actions (animations, movement)
5. **Humanizer** - Adds organic feel (latency, mistakes, cooldowns)

### Hook Strategy
- **Target**: `RE::Actor::Update(float a_delta)` (virtual function at offset 0xAD)
- **Method**: Trampoline hook (pre-hook pattern)
- **Flow**: Hook → ScanAndDecide() → Execute → Vanilla Update (if needed)

## Implementation Steps

### Phase 1: Project Foundation
1. **Create Precompiled Header** (`src/pch.h`)
   - Include CommonLibSSE-NG headers
   - Include standard library headers
   - Define common types and constants

2. **Create Plugin Entry Point** (`src/main.cpp`)
   - SKSE plugin load handler
   - Initialize trampoline
   - Register hooks
   - Initialize CombatDirector

3. **Create Logger Setup** (`src/Logger.h/cpp`)
   - Configure SKSE logger
   - Helper macros for logging

### Phase 2: Core Data Structures

4. **Create ActorStateData** (`src/ActorStateData.h`)
   - Self state: stamina %, health %, animation state, blocking state
   - Target state: action, orientation, vulnerability, distance
   - Helper methods for calculations (dot product, distance, etc.)

5. **Create DecisionResult** (`src/DecisionResult.h`)
   - Enum for action types (Bash, Strafe, Retreat, None)
   - Action parameters (direction, intensity)
   - Priority level

### Phase 3: CombatDirector (Singleton)

6. **Create CombatDirector** (`src/CombatDirector.h/cpp`)
   - Singleton pattern implementation
   - Track active actors in combat
   - Process actors per frame/tick
   - Coordinate between Observer, Decision, and Executor

### Phase 4: State Observation

7. **Create ActorStateObserver** (`src/ActorStateObserver.h/cpp`)
   - Gather self state:
     * `GetActorValue(ActorValue::kStamina)` / `GetBaseActorValue(ActorValue::kStamina)`
     * `GetActorValue(ActorValue::kHealth)` / `GetBaseActorValue(ActorValue::kHealth)`
     * `AsActorState()->GetAttackState()` - check if attacking
     * `IsBlocking()` - check blocking state
   - Gather target state:
     * `GetCombatTarget()` from `CombatController`
     * Check target's `IsBlocking()`, `IsAttacking()`, `IsCasting()`
     * Calculate dot product for orientation
     * Check `GetKnockState()` for vulnerability
     * Calculate distance using positions
   - Return `ActorStateData` structure

### Phase 5: Decision Logic

8. **Create DecisionMatrix** (`src/DecisionMatrix.h/cpp`)
   - Priority 1: Interrupt (Counter-Play)
     * Trigger: Target power attacking + Distance < Reach
     * Action: Force Bash
   - Priority 2: Evasion (Tactical)
     * Trigger: Target blocking + Self idle
     * Action: Force Strafe/Circle
   - Priority 3: Survival (Fallback)
     * Trigger: Stamina < 20% OR Health Critical
     * Action: Force Retreat + Suppress Attack
   - Return `DecisionResult`

### Phase 6: Action Execution

9. **Create ActionExecutor** (`src/ActionExecutor.h/cpp`)
   - Animation Injection:
     * Use `NotifyAnimationGraph(BSFixedString("bashStart"))` for bash
     * Use `NotifyAnimationGraph(BSFixedString("attackStart"))` for attacks
     * Check if actor implements `IAnimationGraphManagerHolder`
   - Movement Override:
     * Access `GetActorRuntimeData().movementController`
     * Cast to `MovementControllerNPC*`
     * Use `IMovementDirectControl` interface (if available)
     * OR manipulate movement through animation graph variables
     * Set strafe direction (perpendicular to target)
   - Cooldown management
   - Action validation (can't bash if already bashing, etc.)

### Phase 7: Humanizer

10. **Create Humanizer** (`src/Humanizer.h/cpp`)
    - Reaction Latency:
      * Configurable base delay (150ms default)
      * Random variance (0-100ms)
      * Per-actor tracking
    - Mistake Probability:
      * Inverse scaling with NPC level
      * Level 1 = 40% failure chance
      * Level 50 = 0% failure chance
      * Formula: `failureChance = max(0, 0.4 - (level * 0.008))`
    - Cooldowns:
      * Bash cooldown: 3 seconds
      * Track per-actor action timestamps
      * Prevent spam/stunlock

### Phase 8: Hook Implementation

11. **Create ActorUpdateHook** (`src/Hooks.h/cpp`)
    - Hook `RE::Actor::Update(float a_delta)`
    - Pre-hook logic:
      * Check if actor is in combat (`IsInCombat()`)
      * Check if actor is NPC (not player)
      * Check if actor is alive
      * Get CombatDirector instance
      * Call `CombatDirector::ProcessActor(actor, delta)`
    - If our logic took action, optionally suppress/modify vanilla call
    - Otherwise, call original function via trampoline

12. **Hook Registration** (in `main.cpp`)
    - Get trampoline: `SKSE::GetTrampoline()`
    - Allocate space: `trampoline.allocate(128)` (adjust as needed)
    - Write hook: Use REL::Relocation for function address
    - Install hook using trampoline

### Phase 9: Integration & Polish

13. **Configuration System** (Optional)
    - Use SimpleINI (already in xmake.lua)
    - Configurable thresholds (stamina %, reaction delay, etc.)
    - Per-NPC-type settings

14. **Error Handling & Safety**
    - Null pointer checks everywhere
    - Validate actor handles
    - Fail silently in Update loops
    - Log errors using SKSE logger

15. **Performance Optimization**
    - Only process actors in combat
    - Throttle processing (not every frame, maybe every 100ms)
    - Cache expensive calculations
    - Use efficient data structures

## File Structure

```
src/
├── pch.h                    # Precompiled header
├── main.cpp                 # Plugin entry point
├── Logger.h/cpp             # Logging utilities
├── CombatDirector.h/cpp     # Singleton manager
├── ActorStateData.h         # Data structures
├── DecisionResult.h         # Decision enums
├── ActorStateObserver.h/cpp # State gathering
├── DecisionMatrix.h/cpp     # Tactical logic
├── ActionExecutor.h/cpp     # Action execution
├── Humanizer.h/cpp          # Organic feel
└── Hooks.h/cpp              # Hook implementations
```

## Key API Usage

### Actor State
- `actor->GetActorValue(RE::ActorValue::kStamina)` - Current stamina
- `actor->GetBaseActorValue(RE::ActorValue::kStamina)` - Max stamina
- `actor->AsActorState()->GetAttackState()` - Attack state enum
- `actor->IsBlocking()` - Blocking check
- `actor->IsInCombat()` - Combat check

### Animation
- `actor->NotifyAnimationGraph(BSFixedString("bashStart"))` - Trigger bash
- Actor must implement `IAnimationGraphManagerHolder`

### Combat
- `actor->GetActorRuntimeData().combatController` - Get combat controller
- `combatController->targetHandle` - Get target handle
- `RE::Actor::LookupByHandle(handle)` - Resolve handle to actor

### Movement
- `actor->GetActorRuntimeData().movementController` - Get movement controller
- Cast to `MovementControllerNPC*` for NPC-specific control
- May need to use animation graph variables for movement

## Testing Strategy

1. **Unit Tests** (where possible)
   - DecisionMatrix logic (pure functions)
   - Humanizer calculations
   - State data calculations

2. **In-Game Testing**
   - Test with different NPC levels
   - Test with different combat styles
   - Verify no crashes
   - Check performance impact
   - Validate AI behavior feels organic

## Notes

- All engine pointers are managed by the game - never delete them
- Use `NiPointer<Actor>` for safe actor references
- Validate all pointers before use
- Log errors but don't crash the game
- Performance is critical - optimize hot paths
