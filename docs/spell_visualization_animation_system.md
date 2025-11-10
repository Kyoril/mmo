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
- **IAudio**: 3D positional audio system for spell sounds with distance attenuation

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
  - Play 3D positional sounds at actor position (if specified)
  - ApplyTintToActor() → Apply color tint to all visible SubEntities
```

## Color Tint System

### Overview

The tint system allows spells to colorize the entire character model by applying a **Vector4 "Tint" parameter** to all visible SubEntity materials. This is useful for visual feedback (e.g., frozen = blue tint, burning = red tint).

**Key Features:**
- ✅ **Multiple Simultaneous Tints**: Multiple spells can tint the same character
- ✅ **Automatic Blending**: Tints blend multiplicatively when stacked
- ✅ **Default Gray Tint**: Base color is `(0.5, 0.5, 0.5)` when no tints are active
- ✅ **Unit-Managed**: GameUnitC manages tints, works with equipment/customization systems
- ✅ **No MaterialInstance Creation**: Tints applied directly to existing materials

### How It Works

1. **GameUnitC Ownership**: 
   - Each `GameUnitC` maintains a `m_spellTints` map (spellId → tint color)
   - `AddSpellTint(spellId, color)` adds/updates a tint
   - `RemoveSpellTint(spellId)` removes a tint
   - `GetBlendedTint()` calculates the combined tint from all active spells

2. **Tint Blending**: When multiple tints are active:
   - Starts with default gray: `(0.5, 0.5, 0.5)`
   - Applies each tint multiplicatively based on its alpha
   - Formula: `result = lerp(result, result * tint.rgb, tint.a)`
   - **Example**: Blue freeze (0.0, 0.5, 1.0, 0.6) + Red burn (1.0, 0.3, 0.0, 0.7) = Purple tint

3. **Material Update**: When tints change:
   - `UpdateTintOnMaterials()` iterates all visible SubEntities
   - Calls `material->SetVectorParameter("Tint", blendedTint)` on each
   - Works with existing materials (no MaterialInstance creation needed)

4. **Cleanup**: Tints are automatically removed on:
   - **Terminating Events**: `CANCEL_CAST`, `CAST_SUCCEEDED` for caster
   - **Aura Removal**: `AURA_REMOVED` for targets
   - When a spell's tint is removed:
     - If other tints remain: Recalculates and applies new blended tint
     - If no tints remain: Applies default gray tint `(0.5, 0.5, 0.5)`

### Integration with Customization/Equipment

**The tint system works seamlessly with all material management:**

- ✅ **Equipment Changes**: When equipment changes a SubEntity's material, `UpdateTintOnMaterials()` applies the current blended tint to the new material
- ✅ **Character Customization**: Visibility changes don't affect tints (only visible SubEntities are tinted)
- ✅ **MaterialInstances**: Equipment system can use MaterialInstances, tint parameter is set directly on them
- ✅ **No Conflicts**: Tint is just a material parameter, doesn't interfere with material swapping

**Call `UpdateTintOnMaterials()` after equipment/customization changes to ensure tints persist.**

### Tint Configuration

Tints are defined in the `tint` field of a SpellKit:

```protobuf
kits {
  event: START_CAST
  tint {
    r: 0.0
    g: 0.5
    b: 1.0
    a: 0.5  # Alpha controls tint intensity
  }
  animation_name: "cast_start"
}
```

**RGBA Color Space:**
- `r`, `g`, `b`: Color channels (0.0 to 1.0)
- `a`: Alpha/intensity (0.0 = no tint, 1.0 = full tint)

**Default Tint (No Spells Active):**
- Color: `(0.5, 0.5, 0.5)` - neutral gray
- This is the base that tints multiply against

**Example Tints:**
- **Frozen**: `r: 0.0, g: 0.5, b: 1.0, a: 0.6` (blue tint)
- **Burning**: `r: 1.0, g: 0.3, b: 0.0, a: 0.7` (red-orange tint)
- **Poisoned**: `r: 0.0, g: 0.8, b: 0.2, a: 0.5` (green tint)
- **Holy**: `r: 1.0, g: 1.0, b: 0.8, a: 0.4` (golden tint)

**Blending Example:**
- Frozen (blue 0.6 alpha) + Burning (red 0.7 alpha) = Purple-ish mixed tint
- Start: `(0.5, 0.5, 0.5)`
- After Frozen: `(0.5, 0.5, 0.7)` - bluish
- After Burning: `(0.65, 0.46, 0.21)` - purple-brown mix

### Material Shader Requirements

For tinting to work, the character's materials must support the "Tint" parameter:

```hlsl
// In your pixel shader:
float4 Tint; // Material parameter (default should be 0.5, 0.5, 0.5, 1.0)

float4 PS_Main(...) : SV_Target
{
    float4 baseColor = texColor * diffuseColor;
    // Apply tint: lerp(baseColor, baseColor * Tint.rgb, Tint.a)
    float4 tintedColor = lerp(baseColor, baseColor * Tint.rgb, Tint.a);
    return tintedColor;
}
```

**Important:** Set the default "Tint" parameter to `(0.5, 0.5, 0.5, 1.0)` in your materials to ensure proper blending.

If the shader doesn't use the "Tint" parameter, it will be ignored (no visual effect).

## Sound System

### 3D Positional Audio

All spell sounds use **3D positional audio** with distance attenuation:

- **Sound Type**: `Sound3D` for one-shot, `SoundLooped3D` for looped
- **Position**: Set to the actor's world position
- **Attenuation**: 
  - **Minimum distance**: 10 units (full volume)
  - **Maximum distance**: 50 units (silent)
  - Linear falloff between min and max
- **Benefits**:
  - Nearby spells are louder
  - Distant spells are quieter or inaudible
  - No audio clutter from many simultaneous casts
  - Realistic spatial audio experience

### Sound Configuration

Sounds are defined in the `sounds` array of a SpellKit:

```protobuf
kits {
  sounds: "Sound/Spells/Frostbolt_Cast.ogg"
  sounds: "Sound/Spells/Frostbolt_Whoosh.ogg"  # Multiple sounds supported
}
```

**Looped vs One-Shot:**
- If `loop: true` → Sound loops continuously until terminating event
- If `loop: false` → Sound plays once

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

### Debuff Spell with Tint (e.g., Frost Nova - Frozen)

```protobuf
# AURA_APPLIED event - freeze target with blue tint
kits_by_event {
  key: 5  # AURA_APPLIED
  value {
    kits {
      animation_name: "Frozen"
      loop: true  # Hold frozen pose
      sounds: "Spells/FrostNova_Apply.ogg"
      tint {
        r: 0.0
        g: 0.5
        b: 1.0
        a: 0.6  # Strong blue tint for frozen effect
      }
      scope: TARGET
    }
  }
}

# AURA_REMOVED event - unfreeze, remove tint
kits_by_event {
  key: 6  # AURA_REMOVED
  value {
    kits {
      sounds: "Spells/FrostNova_Break.ogg"
      scope: TARGET
      # Tint automatically removed when aura is removed
    }
  }
}
```

**What Happens:**

1. **AURA_APPLIED**: Target plays "Frozen" animation (looped), all visible SubEntities get blue tint (60% intensity)
2. **During Aura**: Target remains frozen with blue tint, can't move
3. **AURA_REMOVED**: "Frozen" animation stops, blue tint is automatically removed, original materials restored
4. **Material Restoration**: If target changed equipment during freeze, tint is lost (future enhancement)

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

### Tint Application and Cleanup

**SpellVisualizationService (Simple Delegation):**
```cpp
void SpellVisualizationService::ApplyTintToActor(
    const proto_client::SpellKit& kit, 
    GameUnitC& actor,
    uint32 spellId)
{
    if (!kit.has_tint())
        return;
    
    const auto& tintProto = kit.tint();
    Vector4 tintColor(tintProto.r(), tintProto.g(), tintProto.b(), tintProto.a());
    
    // Delegate to GameUnitC
    actor.AddSpellTint(spellId, tintColor);
}

void SpellVisualizationService::RemoveTintFromActor(
    GameUnitC& actor, 
    uint32 spellId)
{
    // Delegate to GameUnitC
    actor.RemoveSpellTint(spellId);
}
```

**GameUnitC (Tint Management):**
```cpp
void GameUnitC::AddSpellTint(uint32 spellId, const Vector4& tintColor)
{
    // Add or update the tint for this spell
    m_spellTints[spellId] = tintColor;
    
    // Update all SubEntity materials with the new blended tint
    UpdateTintOnMaterials();
}

void GameUnitC::RemoveSpellTint(uint32 spellId)
{
    // Remove the tint for this spell
    m_spellTints.erase(spellId);
    
    // Update all SubEntity materials with the recalculated tint
    UpdateTintOnMaterials();
}

Vector4 GameUnitC::GetBlendedTint() const
{
    if (m_spellTints.empty())
        return Vector4(0.5, 0.5, 0.5, 1.0); // Default gray
    
    Vector4 result(0.5, 0.5, 0.5, 1.0);
    
    // Blend all active tints multiplicatively
    for (const auto& [spellId, tint] : m_spellTints)
    {
        // Lerp between result and (result * tint.rgb) based on tint.a
        result.x = result.x * (1.0 - tint.w) + (result.x * tint.x) * tint.w;
        result.y = result.y * (1.0 - tint.w) + (result.y * tint.y) * tint.w;
        result.z = result.z * (1.0 - tint.w) + (result.z * tint.z) * tint.w;
    }
    
    return result;
}

void GameUnitC::UpdateTintOnMaterials()
{
    if (!m_entity) return;
    
    const Vector4 blendedTint = GetBlendedTint();
    
    // Update all visible SubEntity materials
    for (uint32 i = 0; i < m_entity->GetNumSubEntities(); ++i)
    {
        SubEntity* subEntity = m_entity->GetSubEntity(i);
        if (!subEntity || !subEntity->IsVisible()) continue;
        
        MaterialPtr material = subEntity->GetMaterial();
        if (material)
        {
            // Set tint parameter directly on existing material
            material->SetVectorParameter("Tint", blendedTint);
        }
    }
}
```

**Key Points:**

- ✅ **Unit-Managed**: GameUnitC owns tint state, not SpellVisualizationService
- ✅ **No MaterialInstance Creation**: Tints applied to existing materials (works with equipment system's MaterialInstances)
- ✅ **Multi-Tint Tracking**: `m_spellTints` map (spellId → tint color) in GameUnitC
- ✅ **Automatic Blending**: Calculated on-demand via `GetBlendedTint()`
- ✅ **Equipment Compatible**: When equipment changes materials, call `UpdateTintOnMaterials()` to reapply tints
- ✅ **Visibility Aware**: Only visible SubEntities are tinted

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

- **Particle Effects**: VFX integration for spell visuals
- **Projectiles**: Missile system for ranged spells
- **Animation Blending**: Smooth transitions between states
- **Conditional Kits**: Different animations based on context (weapon type, stance, etc.)
- **Auto-Tint Reapplication**: Hook into equipment system to auto-call `UpdateTintOnMaterials()` on material changes

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

### Tint Not Visible

1. **Shader Support**: Verify the character's material shader uses the "Tint" Vector4 parameter
   - Check pixel shader for `float4 Tint;` declaration
   - Ensure tint is applied in shader: `lerp(baseColor, baseColor * Tint.rgb, Tint.a)`
   - **Important**: Set default "Tint" value to `(0.5, 0.5, 0.5, 1.0)` in material
2. **Alpha Value**: Check `tint.a` is not 0.0 (0 = no tint, 1 = full tint)
3. **SubEntity Visibility**: Tints are only applied to visible SubEntities
   - Character customization may hide certain SubEntities
4. **Equipment Changes**: Call `UpdateTintOnMaterials()` after changing equipment to reapply tints
   - Equipment system changes materials, tint needs to be reapplied
   - This is handled automatically by SpellVisualizationService, but equipment system should call it

### Tint Not Removed

1. **Terminating Events**: Tints are removed on:
   - `CANCEL_CAST` for casters
   - `CAST_SUCCEEDED` for casters
   - `AURA_REMOVED` for targets (via `NotifyAuraVisualizationRemoved`)
2. **Spell ID Mismatch**: Ensure the same spell ID is used for apply/remove
3. **Actor GUID**: Tints are tracked by actor GUID, ensure actor still exists

### Multiple Tints Blending

- ✅ **Supported**: Multiple spells can tint the same actor simultaneously
- ✅ **Automatic Blending**: Tints blend multiplicatively starting from gray `(0.5, 0.5, 0.5)`
- ✅ **Per-Spell Removal**: Removing one spell recalculates blend from remaining tints
- **Example Behavior**:
  1. Apply Frozen (blue) → Character is blue-tinted
  2. Apply Burning (red) → Character becomes purple-ish (blue + red blend)
  3. Remove Frozen → Character returns to red-tinted (only Burning remains)
  4. Remove Burning → Character returns to normal (no tints, restores original material)

### Tint Color Looks Wrong

1. **Default Gray Baseline**: All tints multiply against `(0.5, 0.5, 0.5)` gray
   - Pure colors will appear darker than expected
   - Use brighter RGB values to compensate (e.g., `r: 1.0` instead of `r: 0.5` for red)
2. **Alpha Intensity**: Lower alpha = subtle tint, higher alpha = strong tint
   - `a: 0.3` = 30% tint strength
   - `a: 1.0` = 100% tint strength
3. **Multiplicative Blending**: Tints multiply, not add
   - Blue `(0.0, 0.5, 1.0)` + Red `(1.0, 0.3, 0.0)` ≠ White
   - Result is darker purple-brown due to multiplication

