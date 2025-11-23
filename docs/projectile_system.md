# Projectile System Documentation

## Overview
The new data-driven projectile system replaces the old hardcoded `SpellProjectile` class with a flexible, configurable system that supports multiple motion types, visual effects, and particle trails.

## Architecture

### Components

#### 1. **ProjectileManager** (`game_client/projectile_manager.h/cpp`)
Centralized manager for all active projectiles in the game.

**Key Features:**
- Spawns projectiles based on spell data
- Updates all active projectiles each frame
- Emits `projectileImpact` signal when projectiles hit targets
- Integrates with spell visualization system

**API:**
```cpp
class ProjectileManager
{
public:
    void SpawnProjectile(const proto_client::SpellEntry& spell,
                        const proto_client::SpellVisualization* visualization,
                        GameUnitC* caster,
                        GameUnitC* target);
    
    void Update(float deltaTime);
    void Clear();
    
    signal<void(const proto_client::SpellEntry&, GameUnitC*)> projectileImpact;
};
```

#### 2. **Projectile** (Individual projectile instance)
Represents a single projectile in flight.

**Lifecycle:**
1. Created with start position and target
2. Updated each frame to move toward target
3. Destroyed on impact, triggering visualization events

**Visual Components:**
- Optional mesh entity (e.g., arrow, fireball model)
- Optional particle trail emitter
- Optional looping flight sounds
- Configurable scale and rotation

### Motion Types

The system supports four motion types via `ProjectileMotion` enum:

#### 1. **LINEAR** (Default)
- Straight line from caster to target
- Continuously tracks moving targets
- Best for: Arrows, bullets, magic missiles

#### 2. **ARC**
- Ballistic trajectory with configurable arc height
- Parabolic motion peaking at 50% travel
- Best for: Thrown objects, grenades, catapult projectiles

**Configuration:**
```protobuf
optional float arc_height = 2 [default = 0.0];  // Height in world units
```

#### 3. **HOMING**
- Gradually turns toward moving target
- Configurable turn rate (homing strength)
- Best for: Heat-seeking missiles, guided spells

**Configuration:**
```protobuf
optional float homing_strength = 5 [default = 5.0];  // Turn rate
```

#### 4. **SINE_WAVE**
- Oscillates side-to-side while moving forward
- Configurable frequency and amplitude
- Best for: Serpent projectiles, lightning bolts

**Configuration:**
```protobuf
optional float wave_frequency = 3 [default = 1.0];
optional float wave_amplitude = 4 [default = 1.0];
```

## Data Configuration

### Proto Definition (`spell_visualizations.proto`)

```protobuf
message ProjectileVisual
{
    // Movement
    optional ProjectileMotion motion = 1 [default = LINEAR];
    optional float arc_height = 2 [default = 0.0];
    optional float wave_frequency = 3 [default = 1.0];
    optional float wave_amplitude = 4 [default = 1.0];
    optional float homing_strength = 5 [default = 5.0];

    // Visual representation
    optional string mesh_name = 6;           // e.g., "Models/Arrow.hmsh"
    optional string material_name = 7;       // Material override
    optional string trail_particle = 8;      // Particle system name
    optional float scale = 9 [default = 1.0];

    // Rotation
    optional bool face_movement = 10 [default = true];  // Orient along velocity
    optional float spin_rate = 11 [default = 0.0];      // Degrees/second

    // Effects
    repeated string sounds = 12;              // Flight sounds
    optional string impact_particle = 13;     // Impact effect (TODO)
}
```

### Example Configurations

#### Arrow
```json
{
    "motion": "LINEAR",
    "mesh_name": "Models/Arrow.hmsh",
    "face_movement": true,
    "scale": 1.0
}
```

#### Fireball
```json
{
    "motion": "LINEAR",
    "mesh_name": "Models/Fireball.hmsh",
    "trail_particle": "FireTrail",
    "scale": 1.5,
    "sounds": ["Sound/Spells/FireWhoosh.ogg"],
    "spin_rate": 360.0
}
```

#### Homing Missile
```json
{
    "motion": "HOMING",
    "homing_strength": 3.0,
    "mesh_name": "Models/Missile.hmsh",
    "trail_particle": "SmokeTrail",
    "face_movement": true
}
```

#### Catapult Stone
```json
{
    "motion": "ARC",
    "arc_height": 10.0,
    "mesh_name": "Models/Rock.hmsh",
    "spin_rate": 180.0
}
```

## Integration

### WorldState Integration
The `ProjectileManager` is created in `WorldState::OnEnter()` and integrated with the spell casting system:

```cpp
// In OnEnter():
m_projectileManager = std::make_unique<ProjectileManager>(*m_scene, &m_audio);

// Connect impact signal to visualization service
m_projectileManager->projectileImpact.connect([](const SpellEntry& spell, GameUnitC* target)
{
    std::vector<GameUnitC*> targets;
    if (target) targets.push_back(target);
    SpellVisualizationService::Get().Apply(Event::Impact, spell, nullptr, targets);
});
```

### Spell Casting Flow

1. **Server sends SMSG_SPELL_GO** packet
2. **WorldState checks spell.speed()**
   - If `speed > 0`: Spawn projectile via ProjectileManager
   - If `speed == 0`: Instant cast, dispatch Impact event directly
3. **ProjectileManager updates** projectiles each frame
4. **On impact**: ProjectileManager emits `projectileImpact` signal
5. **Signal handler** calls `SpellVisualizationService::Apply(Event::Impact, ...)`
6. **Visualization service** applies impact kits (sounds, particles, animations)

## Features

### ✅ Implemented
- Multiple motion types (linear, arc, homing, sine wave)
- Mesh-based projectile visuals
- Particle trail support
- Flight sound effects (looped 3D audio)
- Automatic target tracking
- Smooth rotation (face movement direction)
- Configurable spin rate
- Scale support
- Integration with spell visualization system
- Signal-based impact events

### 🚧 TODO
- Impact particle effects (defined but not yet spawned)
- Projectile pooling/recycling for performance
- Multi-target projectiles (e.g., chain lightning)
- Deflection/bounce mechanics
- Speed modifiers during flight
- Death of caster/target handling improvements

## Migration from Old System

### Removed
- `SpellProjectile` class (hardcoded sphere mesh)
- Direct projectile management in WorldState
- Hardcoded sphere visual ("Models/Sphere.hmsh")

### Benefits
- **Data-driven**: Configure projectiles in proto files
- **Flexible**: Support multiple motion types without code changes
- **Extensible**: Easy to add new motion types or visual effects
- **Integrated**: Works seamlessly with spell visualization system
- **Performant**: Centralized update loop, signal-based events

## Performance Considerations

- **Impact Check**: Distance-based collision detection (0.5 unit threshold)
- **Update Frequency**: Once per frame for all projectiles
- **Cleanup**: Automatic cleanup of scene objects on impact/destruction
- **Sound Management**: Proper 3D audio cleanup to prevent leaks

## Future Enhancements

1. **Projectile Pooling**: Reuse projectile objects to reduce allocations
2. **Collision System**: Integrate with physics for terrain/obstacle collision
3. **Beam Effects**: Support for continuous beam projectiles
4. **Area Projectiles**: Projectiles that affect areas (e.g., meteor shower)
5. **Ricochet**: Bouncing projectiles
6. **Speed Curves**: Non-constant speed over lifetime
7. **Multi-Target**: Projectiles that split or chain to multiple targets
