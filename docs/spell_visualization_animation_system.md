# Spell Visualization Animation System

## Overview

The spell visualization system provides fully data-driven animation playback for spell events. Animations are defined in `SpellKit` entries within spell visualization data and are applied automatically when spell events occur.

## Key Principles

1. **Fully Data-Driven**: All animations come from spell visualization data, not hardcoded
2. **No Visualization = No Animation**: If a spell has no `visualization_id`, nothing visual or audible happens
3. **No Event Kits = No Animation**: If a spell event has no kits defined, no animation plays for that event
4. **Animation Names Must Exist**: The `animation_name` field must reference an existing animation in the character's skeleton

## Architecture

### Components

- **SpellVisualizationService**: Core service that resolves spell events to visualization kits
- **SpellKit**: Data structure containing animation, sound, tint, and timing info
- **GameUnitC**: Unit class that plays animations via `SetTargetAnimState()` or `PlayOneShotAnimation()`
- **WorldState**: Network packet handler that triggers visualization events on spell start/go/failure

### Flow

```
Network Packet Received (SpellStart/SpellGo/SpellFailure)
  ↓
WorldState::OnSpellStart/OnSpellGo/OnSpellFailure
  ↓
SpellVisualizationService::Apply()
  ↓
Lookup spell.visualization_id
  ↓
Find kits for event (START_CAST, CASTING, CAST_SUCCEEDED, etc.)
  ↓
For each kit:
  - ApplyAnimationToActor() → Play animation
  - Play sounds (if specified)
  - Apply tint (TODO)
```

### Legacy System Removed

The old hardcoded animation system has been completely removed:
- ❌ `GameUnitC::NotifySpellCastStarted()` - now empty (legacy compatibility)
- ❌ `GameUnitC::NotifySpellCastCancelled()` - now empty (legacy compatibility)
- ❌ `GameUnitC::NotifySpellCastSucceeded()` - now empty (legacy compatibility)
- ❌ `GameUnitC::m_casting` flag - removed entirely
- ❌ Hardcoded casting state checks in movement animations - removed

All spell animations are now **100% data-driven** from spell visualization entries.

## Animation Playback Modes

### Looped Animations

Used for continuous states (e.g., channeling spells, auras).

**Configuration:**
```protobuf
animation_name: "CastLoop"
loop: true
```

**Behavior:**
- Sets animation as the target state via `SetTargetAnimState()`
- Animation continues until another animation replaces it
- Common for: CASTING, AURA_IDLE events

### One-Shot Animations

Used for discrete actions (e.g., spell release, impact).

**Configuration:**
```protobuf
animation_name: "CastRelease"
loop: false
duration_ms: 500
```

**Behavior:**
- Plays as overlay via `PlayOneShotAnimation()`
- Duration controls playback speed (animation scaled to fit duration)
- Animation returns to previous state when complete
- **Automatically canceled by subsequent events from the same spell**
- Common for: CAST_SUCCEEDED, IMPACT, AURA_APPLIED events

### Animation Interruption System

**Problem Solved:**
When a spell cast fails, the cast_start animation might still be playing and won't be interrupted. This looked wrong because characters continued their casting animation even after the spell was canceled.

**Solution:**
The system now tracks active one-shot animations per spell and automatically cancels them when a **terminating event** from the same spell occurs.

**Terminating Events (stop animations):**
- `CANCEL_CAST` - Spell was interrupted or canceled
- `CAST_SUCCEEDED` - Spell completed successfully

**Non-Terminating Events (allow animations to continue):**
- `START_CAST` - Initial cast event
- `CASTING` - Ongoing cast loop (allows cast_start to finish naturally)
- `IMPACT`, `AURA_APPLIED`, etc. - These don't interrupt the main casting flow

**How It Works:**

1. **Per-Spell Tracking**: Each actor tracks which spell is currently playing a one-shot animation
2. **Terminating Event Interruption**: `CANCEL_CAST` and `CAST_SUCCEEDED` stop active animations from the same spell
3. **Progressive Events Coexist**: `cast_start` animation can play alongside `cast_loop` without interruption
4. **Different-Spell Isolation**: Proc spells or simultaneous spells won't interfere with each other
5. **Automatic Fallback**: If a terminating event has no animation kits, the system automatically returns to movement/idle animations

**Example Flow (Normal Cast):**

```
START_CAST event → cast_start animation (one-shot, 800ms) starts
  ↓ (100ms passes)
CASTING event → cast_loop animation (looped) starts
  ↓
  cast_start continues playing as overlay (not canceled!)
  cast_loop plays as base animation
  ↓ (700ms passes, cast_start finishes naturally)
  cast_loop continues
  ↓ (cast completes after 2000ms total)
CAST_SUCCEEDED event (has kits) → cast_start/cast_loop STOPPED, cast_release plays
```

**Example Flow (Canceled Cast - No Animation):**

```
START_CAST event → cast_start animation (800ms) stored in m_oneShotState
  ↓ (300ms passes)
CANCEL_CAST event (no kits defined) → CancelOneShotAnimation() called
  ↓
m_oneShotState cleared and disabled
  ↓
RefreshMovementAnimation() called automatically
  ↓
Character returns to idle/run animation (proper state management, no T-pose!)
```

**Example Flow (Canceled Cast - With Animation):**

```
START_CAST event → cast_start animation (800ms)
  ↓ (300ms passes)
CANCEL_CAST event (has cast_interrupt animation) → cast_start STOPPED, cast_interrupt plays
  ↓
cast_interrupt finishes
  ↓
Movement animations resume
```

**Benefits:**

- ✅ Cast failures immediately stop casting animations and return to movement state
- ✅ No T-pose issues - automatic fallback to idle/run animations
- ✅ Cast success cleanly transitions to release animation
- ✅ cast_start and cast_loop can play together naturally (overlay + base)
- ✅ Proc spells don't interrupt the main spell's animation (different spell IDs)
- ✅ No manual animation state management needed
- ✅ Designers have full control: define cancel animation for custom interrupt, omit for instant return to movement

**Configuration Example:**

```protobuf
# Frostbolt spell visualization
kits_by_event {
  key: 0  # START_CAST
  value {
    kits {
      animation_name: "cast_start"
      loop: false
      duration_ms: 800
    }
  }
}
kits_by_event {
  key: 2  # CASTING
  value {
    kits {
      animation_name: "cast_loop"
      loop: true
    }
  }
}
kits_by_event {
  key: 1  # CANCEL_CAST - no animation (instant return to movement)
  value {
    # Empty - cast_start/cast_loop will be stopped, character returns to idle/run
  }
}
kits_by_event {
  key: 3  # CAST_SUCCEEDED
  value {
    kits {
      animation_name: "cast_release"
      loop: false
      duration_ms: 500
    }
  }
}
```

**Alternative: Cancel With Interrupt Animation**

```protobuf
kits_by_event {
  key: 1  # CANCEL_CAST - with interrupt animation
  value {
    kits {
      animation_name: "cast_interrupt"  # Stops cast_start/cast_loop, plays interrupt
      loop: false
      duration_ms: 300
    }
  }
}
```

**What Happens:**

1. **START_CAST**: `cast_start` animation plays for 800ms (one-shot overlay)
2. **CASTING** (if spell has cast time): `cast_loop` starts as base animation, `cast_start` continues as overlay
3. **CANCEL_CAST (no kits)**: `cast_start`/`cast_loop` stopped, system automatically returns to idle/run animation
4. **CANCEL_CAST (with kits)**: `cast_start`/`cast_loop` stopped, `cast_interrupt` plays, then returns to idle/run
5. **CAST_SUCCEEDED**: `cast_release` animation plays for 500ms AND stops any previous animations from this spell

**Key Insights**: 
- `cast_start` and `cast_loop` can play simultaneously! The one-shot `cast_start` overlays on top of the looped `cast_loop`.
- Terminating events (`CANCEL_CAST`, `CAST_SUCCEEDED`) always stop active spell animations.
- If no replacement animation is defined, `RefreshMovementAnimation()` is called to return to idle/run state.
- This prevents T-pose issues while giving designers full control over interrupt behavior.

## Common Animation Names

These depend on your character skeletons, but typical examples:

- `Idle` - Standing idle
- `Run` - Running
- `cast_start` - Spell cast start (one-shot)
- `cast_loop` - Continuous casting animation (looped)
- `cast_release` - Spell release/finish animation (one-shot)
- `Attack1H` - One-handed melee attack
- `Attack2H` - Two-handed melee attack
- `UnarmedAttack01` - Unarmed attack
- `Hit` - Taking damage
- `Death` - Death animation
- `JumpStart` - Jump start
- `Falling` - Falling loop
- `Land` - Landing

## Spell Events

The system supports 9 spell lifecycle events:

| Event | Value | Typical Use |
|-------|-------|-------------|
| START_CAST | 0 | Spell casting begins (first frame) |
| CANCEL_CAST | 1 | Spell casting interrupted |
| CASTING | 2 | During cast bar progression (looping) |
| CAST_SUCCEEDED | 3 | Cast bar completes successfully |
| IMPACT | 4 | Spell hits target(s) |
| AURA_APPLIED | 5 | Buff/debuff applied to target |
| AURA_REMOVED | 6 | Buff/debuff removed from target |
| AURA_TICK | 7 | Periodic aura effect |
| AURA_IDLE | 8 | Aura present but not ticking |

## Example Configurations

### Instant Cast Spell (e.g., Fireball)

```protobuf
# CAST_SUCCEEDED event
kits_by_event {
  key: 3  # CAST_SUCCEEDED
  value {
    kits {
      animation_name: "CastRelease"
      loop: false
      duration_ms: 400
      sounds: "Spells/Fireball_Cast.ogg"
      scope: CASTER
    }
  }
}

# IMPACT event
kits_by_event {
  key: 4  # IMPACT
  value {
    kits {
      animation_name: "Hit"
      loop: false
      duration_ms: 300
      sounds: "Spells/Fireball_Impact.ogg"
      scope: TARGET
    }
  }
}
```

### Channeled Spell (e.g., Arcane Missiles)

```protobuf
# START_CAST event
kits_by_event {
  key: 0  # START_CAST
  value {
    kits {
      animation_name: "CastLoop"
      loop: true
      sounds: "Spells/ArcaneMissiles_Channel.ogg"
      scope: CASTER
    }
  }
}

# CAST_SUCCEEDED event (end channel)
kits_by_event {
  key: 3  # CAST_SUCCEEDED
  value {
    kits {
      # No animation - just stop the looped animation
      sounds: "Spells/ArcaneMissiles_End.ogg"
      scope: CASTER
    }
  }
}
```

### Buff/Aura Spell (e.g., Power Word: Fortitude)

```protobuf
# AURA_APPLIED event
kits_by_event {
  key: 5  # AURA_APPLIED
  value {
    kits {
      animation_name: "BuffOn"
      loop: false
      duration_ms: 500
      sounds: "Spells/Buff_Applied.ogg"
      tint {
        r: 1.0
        g: 0.8
        b: 0.2
        a: 0.3
      }
      scope: TARGET
    }
  }
}

# AURA_REMOVED event
kits_by_event {
  key: 6  # AURA_REMOVED
  value {
    kits {
      sounds: "Spells/Buff_Removed.ogg"
      scope: TARGET
    }
  }
}
```

## Implementation Details

### Animation Resolution

1. Service checks if spell has `visualization_id`
2. Looks up `SpellVisualization` entry by ID
3. Finds `kits_by_event` map entry for current event
4. Iterates through kits and applies by `scope` (CASTER or TARGET)

### Animation Application (`ApplyAnimationToActor`)

```cpp
void SpellVisualizationService::ApplyAnimationToActor(
    const proto_client::SpellKit& kit, 
    GameUnitC& actor)
{
    // 1. Check if animation_name is specified
    if (!kit.has_animation_name() || kit.animation_name().empty())
        return;
    
    // 2. Get animation state from entity
    AnimationState* animState = entity->GetAnimationState(animName);
    
    // 3. Configure loop mode
    bool isLooped = kit.has_loop() && kit.loop();
    animState->SetLoop(isLooped);
    
    // 4. Adjust playback speed for duration (one-shot only)
    if (!isLooped && kit.has_duration_ms())
    {
        float playRate = animLength / (duration_ms / 1000.0f);
        animState->SetPlayRate(playRate);
    }
    
    // 5. Play animation
    if (isLooped)
        actor.SetTargetAnimState(animState);  // Replace current state
    else
        actor.PlayOneShotAnimation(animState); // Overlay
}
```

### Looped Sound Cleanup

The service automatically stops looped sounds when:
- `CANCEL_CAST` event occurs
- `CAST_SUCCEEDED` event occurs
- Aura is removed via `NotifyAuraVisualizationRemoved()`

## Migration from Legacy System

### Old System (Hardcoded)

```cpp
// OLD: Hardcoded in GameUnitC
void GameUnitC::NotifySpellCastStarted()
{
    m_casting = true;
    // Animation automatically switches to m_castingState
}
```

### New System (Data-Driven)

```protobuf
# NEW: Defined in spell_visualizations.data
spell_visualization {
  id: 100
  name: "Frostbolt Visualization"
  kits_by_event {
    key: 0  # START_CAST
    value {
      kits {
        animation_name: "CastLoop"
        loop: true
      }
    }
  }
}
```

## Best Practices

1. **Use Descriptive Animation Names**: Match skeleton animation names exactly
2. **Test Animation Lengths**: Ensure `duration_ms` feels natural for one-shot animations
3. **Minimize Looped Animations**: Only use for truly continuous states
4. **Handle Missing Animations Gracefully**: Service logs warnings but continues
5. **Scope Correctly**: Use CASTER for self-animations, TARGET for victim reactions
6. **Keep Kits Focused**: One kit = one cohesive effect (animation + matching sound)

## Future Enhancements

- **Tint/Color Effects**: Material overlay system (marked TODO)
- **Particle Effects**: VFX integration for spell visuals
- **Projectiles**: Missile system for ranged spells
- **Animation Blending**: Smooth transitions between states
- **Conditional Kits**: Different animations based on context (weapon type, stance, etc.)

## Troubleshooting

### Animation Not Playing

1. Check spell has `visualization_id` set
2. Verify visualization entry exists in `spell_visualizations.data`
3. Confirm event has kits defined in `kits_by_event`
4. Ensure `animation_name` matches skeleton animation exactly (case-sensitive)
5. Check entity has been created (animations require mesh/skeleton)

### Animation Plays Wrong Speed

- One-shot animations scale to `duration_ms`
- Check `duration_ms` matches desired timing
- Looped animations use default playback rate

### Animation Stuck/Won't Stop

- Looped animations continue until replaced
- Ensure CANCEL_CAST or CAST_SUCCEEDED events stop loops
- Check `StopLoopedSoundForActor()` is called (also stops animation state)
