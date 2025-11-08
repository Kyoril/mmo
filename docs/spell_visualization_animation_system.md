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

### Flow

```
Spell Event Triggered
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
- Common for: CAST_SUCCEEDED, IMPACT, AURA_APPLIED events

## Common Animation Names

These depend on your character skeletons, but typical examples:

- `Idle` - Standing idle
- `Run` - Running
- `CastLoop` - Continuous casting animation
- `CastRelease` - Spell release/finish animation
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
