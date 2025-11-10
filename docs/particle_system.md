# Particle System Documentation

## Overview

The MMO particle system provides a high-performance, flexible solution for creating visual effects such as fire, smoke, magic spells, and environmental effects. The system integrates seamlessly with the scene graph and supports real-time editing through the MMO Edit application.

## Architecture

### Core Components

#### 1. **Particle** (struct)
Represents a single particle in the system. Optimized to be exactly 64 bytes (one cache line) for CPU cache efficiency.

**Key Members:**
- `position` - World space position
- `velocity` - Movement vector
- `color` - RGBA color (Vector4, 0-1 range)
- `size` - Current size
- `lifetime` / `age` - Lifetime management
- `rotation` / `angularVelocity` - Rotation support
- `spriteIndex` - Sprite sheet animation index

#### 2. **ParticleEmitterParameters** (struct)
Contains all configuration data for a particle emitter. This structure is serialized to `.hpar` files.

**Key Categories:**
- **Spawn Settings**: `spawnRate`, `maxParticles`
- **Shape Settings**: `shape`, `shapeExtents`
- **Lifetime**: `minLifetime`, `maxLifetime`
- **Velocity**: `minVelocity`, `maxVelocity`
- **Forces**: `gravity`
- **Size**: `startSize`, `endSize`
- **Color**: `colorOverLifetime` (ColorCurve)
- **Sprite Animation**: `spriteSheetColumns`, `spriteSheetRows`, `animateSprites`
- **Material**: `materialName`

#### 3. **EmitterShape** (enum)
Defines the spawn volume shape:
- `Point` - Single spawn point
- `Sphere` - Spherical volume
- `Box` - Box volume
- `Cone` - Conical volume

#### 4. **ParticleEmitter** (class)
Main particle emitter class inheriting from `MovableObject`. Manages particle lifecycle and integrates with the scene graph.

**Key Methods:**
- `SetParameters(params)` - Configure emitter behavior
- `Play()` / `Stop()` - Control emission
- `Reset()` - Clear all particles
- `Update()` - Update particle simulation (called automatically by scene)
- `SetMaterial(material)` - Assign rendering material

#### 5. **ParticleRenderable** (class)
Handles GPU rendering of particles as camera-facing billboards.

**Key Features:**
- Dynamic vertex/index buffer management
- Automatic back-to-front sorting for alpha blending
- Efficient buffer reallocation strategy
- Billboard generation using camera orientation

## Usage

### Creating a Particle Emitter in Code

```cpp
#include "scene_graph/scene.h"
#include "scene_graph/particle_emitter.h"

// Create emitter
ParticleEmitter* emitter = scene.CreateParticleEmitter("MyEffect");

// Configure parameters
ParticleEmitterParameters params;
params.spawnRate = 50.0f;
params.maxParticles = 200;
params.shape = EmitterShape::Sphere;
params.shapeExtents = Vector3(2.0f, 0.0f, 0.0f); // Radius = 2.0
params.minLifetime = 1.0f;
params.maxLifetime = 3.0f;
params.minVelocity = Vector3(0.0f, 2.0f, 0.0f);
params.maxVelocity = Vector3(0.0f, 5.0f, 0.0f);
params.gravity = Vector3(0.0f, -9.81f, 0.0f);
params.startSize = 1.0f;
params.endSize = 0.0f;
params.colorOverLifetime = ColorCurve(
    Vector4(1.0f, 0.5f, 0.0f, 1.0f),  // Orange, opaque
    Vector4(1.0f, 0.0f, 0.0f, 0.0f)   // Red, transparent
);
params.materialName = "Particles/Additive.hmat";

emitter->SetParameters(params);

// Load and assign material
auto material = MaterialManager::Get().Load(params.materialName);
emitter->SetMaterial(material);

// Attach to scene node
SceneNode* node = scene.GetRootSceneNode().CreateChildSceneNode();
node->AttachObject(*emitter);

// Start emitting
emitter->Play();
```

### Loading from File

```cpp
#include "scene_graph/particle_emitter_serializer.h"
#include "binary_io/stream_source.h"
#include "binary_io/reader.h"

// Load parameters from .hpar file
ParticleEmitterParameters params;
StreamSource source("Effects/Fire.hpar");
io::Reader reader(source);
ParticleEmitterSerializer serializer;

if (serializer.Deserialize(params, reader))
{
    emitter->SetParameters(params);
    
    // Load material
    if (!params.materialName.empty())
    {
        auto material = MaterialManager::Get().Load(params.materialName);
        emitter->SetMaterial(material);
    }
}
```

### Saving to File

```cpp
#include "scene_graph/particle_emitter_serializer.h"
#include "stream_sink.h"
#include "binary_io/writer.h"

ParticleEmitterParameters params = emitter->GetParameters();

StreamSink sink("Effects/Fire.hpar");
io::Writer writer(sink);
ParticleEmitterSerializer serializer;

serializer.Serialize(params, writer);
```

## Particle System Editor

The MMO Edit application includes a visual editor for creating and editing particle systems.

### Opening the Editor

1. Launch `mmo_edit.exe`
2. In the Asset Browser, navigate to a `.hpar` file
3. Double-click the file to open the editor

### Editor Interface

The editor window is divided into two main panels:

#### **Viewport** (Left)
- Real-time preview of the particle system
- **Camera Controls:**
  - Left Mouse: Rotate camera
  - Right Mouse: Pan camera
  - Middle Mouse: Zoom camera

#### **Parameters** (Right)
Multiple collapsible sections for editing:

##### **Spawn Settings**
- **Spawn Rate**: Particles per second
- **Max Particles**: Maximum simultaneous particles

##### **Emitter Shape**
- **Shape**: Dropdown (Point, Sphere, Box, Cone)
- Shape-specific parameters:
  - Sphere: Radius
  - Box: Extents (width, height, depth)
  - Cone: Angle, Height, Base Radius

##### **Particle Properties**
- **Lifetime**: Min/Max lifetime range
- **Velocity**: Min/Max initial velocity
- **Gravity**: Constant acceleration
- **Size**: Start/End size (interpolated over lifetime)
- **Sprite Sheet**: Columns, Rows, Animation toggle

##### **Color Over Lifetime**
- **Start Color**: RGBA color at spawn
- **End Color**: RGBA color at death
- Linear interpolation between colors

##### **Material**
- **Material Name**: Path to material file (e.g., `Particles/Fire.hmat`)
- Quick buttons for default materials:
  - **Use Default Additive**: For bright, glowing effects
  - **Use Default Alpha**: For soft, blended effects

### Playback Controls

Menu bar provides playback options:
- **Play/Pause**: Toggle particle emission
- **Reset**: Clear all particles and restart

### Saving Changes

- **File → Save** (or Ctrl+S): Save parameters to `.hpar` file

## Creating Custom Materials

Particle materials should be created using the Material Editor. Two default materials are provided:

### Default Additive Material
**Path**: `Particles/Additive.hmat`

**Recommended Settings:**
- Blend Mode: Additive
- Depth Write: Disabled
- Depth Test: Enabled
- Cull Mode: None (double-sided)
- Texture: Particle texture with alpha channel

**Best For:**
- Fire, explosions, magic effects
- Bright, glowing particles
- Effects that add light to the scene

### Default Alpha Material
**Path**: `Particles/Alpha.hmat`

**Recommended Settings:**
- Blend Mode: Alpha Blend
- Depth Write: Disabled
- Depth Test: Enabled
- Cull Mode: None (double-sided)
- Texture: Particle texture with alpha channel

**Best For:**
- Smoke, fog, clouds
- Soft effects
- Effects that blend with the background

## Performance Considerations

### Optimization Tips

1. **Particle Count**
   - Keep `maxParticles` as low as possible
   - Mobile: 50-100 particles per emitter
   - Desktop: 200-500 particles per emitter
   - High-end: 500-1000+ particles per emitter

2. **Overdraw**
   - Large particles create more overdraw
   - Use smaller particle sizes when possible
   - Avoid overlapping multiple particle emitters

3. **Sorting**
   - Alpha blending requires back-to-front sorting
   - Sorting is expensive for high particle counts
   - Use additive blending when possible (no sorting needed)

4. **Spawn Rate**
   - Lower spawn rates reduce CPU overhead
   - Burst effects are more efficient than continuous emission

5. **Lifetime**
   - Shorter lifetimes reduce memory and rendering cost
   - Typical range: 0.5 - 5.0 seconds

### Memory Usage

Per-particle memory usage:
- 64 bytes per active particle (cache-optimized)
- Vertex buffer: 4 vertices × 32 bytes = 128 bytes per particle
- Total: ~192 bytes per particle

Example calculations:
- 100 particles ≈ 19 KB
- 500 particles ≈ 94 KB
- 1000 particles ≈ 188 KB

## Advanced Features

### Emitter Shapes

#### Point Emitter
Particles spawn from a single point. Simplest and most efficient.

**Use Cases:**
- Small explosions
- Impact effects
- Sparkles

#### Sphere Emitter
Particles spawn from within a sphere volume.

**Parameters:**
- `shapeExtents.x` = radius

**Use Cases:**
- Ambient effects around objects
- Energy fields
- Area effects

#### Box Emitter
Particles spawn from within a box volume.

**Parameters:**
- `shapeExtents` = full box dimensions (width, height, depth)

**Use Cases:**
- Rain/snow over an area
- Dust clouds
- Room effects

#### Cone Emitter
Particles spawn from within a cone volume.

**Parameters:**
- `shapeExtents.x` = angle in radians
- `shapeExtents.y` = height
- `shapeExtents.z` = base radius

**Use Cases:**
- Directional effects (fire, smoke)
- Spell casts
- Waterfalls

**Note**: Cone orientation is controlled by the parent SceneNode's orientation.

### Color Animation

The `colorOverLifetime` property uses a `ColorCurve` for color animation:

```cpp
// Fade from white to transparent red
params.colorOverLifetime = ColorCurve(
    Vector4(1.0f, 1.0f, 1.0f, 1.0f),  // White, opaque
    Vector4(1.0f, 0.0f, 0.0f, 0.0f)   // Red, transparent
);
```

**Current Implementation:**
- Linear interpolation between start and end colors
- RGBA channels interpolated independently

**Future Enhancement:**
- Multi-point gradient curves
- Custom interpolation curves

### Sprite Sheet Animation

Enable sprite sheet animation for more complex particle visuals:

```cpp
params.spriteSheetColumns = 4;
params.spriteSheetRows = 4;
params.animateSprites = true;
```

**Current Status**: Infrastructure in place, full implementation pending.

**Planned Features:**
- Automatic frame progression based on particle lifetime
- Custom animation speed control
- Random starting frames

## Integration with Scene Graph

### Hierarchy
```
Scene
└── RootSceneNode
    └── ParticleEmitterNode (SceneNode)
        └── ParticleEmitter (MovableObject)
```

### Transforms
- Particle spawn positions are affected by parent node's position and orientation
- Cone emitter direction is controlled by node orientation
- Once spawned, particles move independently in world space

### Visibility
- Controlled via SceneNode visibility
- Supports frustum culling via bounding box

### Render Queue
- Default render queue: Standard
- Rendered with other transparent objects
- Automatic depth sorting for alpha blending

## API Reference

### ParticleEmitter Class

#### Construction
```cpp
// Created via Scene::CreateParticleEmitter
ParticleEmitter* emitter = scene.CreateParticleEmitter("name");
```

#### Configuration
```cpp
void SetParameters(const ParticleEmitterParameters& params);
const ParticleEmitterParameters& GetParameters() const;
void SetMaterial(const MaterialPtr& material);
MaterialPtr GetMaterial() const;
```

#### Playback Control
```cpp
void Play();        // Start emitting particles
void Stop();        // Stop emitting (existing particles continue)
void Reset();       // Clear all particles
bool IsPlaying() const;
```

#### Updates
```cpp
void Update();      // Called automatically by Scene::UpdateSceneGraph
```

#### Static Utilities
```cpp
static MaterialPtr GetDefaultMaterial(bool additive = false);
```

### ParticleEmitterSerializer Class

#### Serialization
```cpp
void Serialize(
    const ParticleEmitterParameters& params,
    io::Writer& writer,
    ParticleEmitterVersion version = particle_emitter_version::Latest
) const;
```

#### Deserialization
```cpp
bool Deserialize(
    ParticleEmitterParameters& params,
    io::Reader& reader
);
```

## File Format

### .hpar File Structure

Particle emitter files use a chunk-based binary format:

```
Header:
- Magic Number: "HPAR" (4 bytes)
- Version: uint32 (currently 0x0100)

Chunks:
- Each chunk has: ID (uint32) + Size (uint32) + Data
- Chunk IDs defined in particle_emitter_serializer.cpp
```

**Supported Chunks:**
- Spawn parameters
- Shape parameters
- Lifetime parameters
- Velocity parameters
- Gravity
- Size parameters
- Color curve
- Sprite sheet parameters
- Material name

## Common Issues and Solutions

### Particles Not Visible

**Possible Causes:**
1. Material not loaded or invalid
   - **Solution**: Check material path, ensure material file exists
2. Emitter not playing
   - **Solution**: Call `emitter->Play()`
3. Spawn rate is 0
   - **Solution**: Set `params.spawnRate > 0`
4. Max particles is 0
   - **Solution**: Set `params.maxParticles > 0`
5. Particles too small
   - **Solution**: Increase `startSize` / `endSize`

### Particles Rendering Incorrectly

**Possible Causes:**
1. Incorrect blend mode
   - **Solution**: Check material blend settings
2. Depth write enabled
   - **Solution**: Disable depth write in material
3. Culling enabled
   - **Solution**: Set cull mode to None
4. Wrong render queue
   - **Solution**: Ensure transparent materials use appropriate queue

### Performance Issues

**Symptoms**: Low FPS, stuttering

**Solutions:**
1. Reduce `maxParticles`
2. Reduce `spawnRate`
3. Decrease particle `lifetime`
4. Use additive blending instead of alpha (no sorting)
5. Optimize particle texture size
6. Reduce number of active emitters

## Examples

### Fire Effect

```cpp
ParticleEmitterParameters fireParams;
fireParams.spawnRate = 30.0f;
fireParams.maxParticles = 100;
fireParams.shape = EmitterShape::Cone;
fireParams.shapeExtents = Vector3(0.5f, 2.0f, 0.5f); // 30° cone, 2m high
fireParams.minLifetime = 0.8f;
fireParams.maxLifetime = 1.5f;
fireParams.minVelocity = Vector3(0.0f, 1.0f, 0.0f);
fireParams.maxVelocity = Vector3(0.0f, 3.0f, 0.0f);
fireParams.gravity = Vector3(0.0f, 0.5f, 0.0f); // Slight upward force
fireParams.startSize = 0.8f;
fireParams.endSize = 0.1f;
fireParams.colorOverLifetime = ColorCurve(
    Vector4(1.0f, 1.0f, 0.0f, 1.0f),  // Bright yellow
    Vector4(1.0f, 0.2f, 0.0f, 0.0f)   // Dark red, fade out
);
fireParams.materialName = "Particles/Additive.hmat";
```

### Smoke Effect

```cpp
ParticleEmitterParameters smokeParams;
smokeParams.spawnRate = 10.0f;
smokeParams.maxParticles = 50;
smokeParams.shape = EmitterShape::Sphere;
smokeParams.shapeExtents = Vector3(0.5f, 0.0f, 0.0f);
smokeParams.minLifetime = 2.0f;
smokeParams.maxLifetime = 4.0f;
smokeParams.minVelocity = Vector3(-0.5f, 1.0f, -0.5f);
smokeParams.maxVelocity = Vector3(0.5f, 2.0f, 0.5f);
smokeParams.gravity = Vector3(0.0f, 0.2f, 0.0f); // Gentle rise
smokeParams.startSize = 0.5f;
smokeParams.endSize = 2.0f; // Grows over time
smokeParams.colorOverLifetime = ColorCurve(
    Vector4(0.5f, 0.5f, 0.5f, 0.8f),  // Gray, mostly opaque
    Vector4(0.3f, 0.3f, 0.3f, 0.0f)   // Darker, fade out
);
smokeParams.materialName = "Particles/Alpha.hmat";
```

### Magic Sparkle Effect

```cpp
ParticleEmitterParameters sparkleParams;
sparkleParams.spawnRate = 50.0f;
sparkleParams.maxParticles = 200;
sparkleParams.shape = EmitterShape::Sphere;
sparkleParams.shapeExtents = Vector3(1.0f, 0.0f, 0.0f);
sparkleParams.minLifetime = 0.5f;
sparkleParams.maxLifetime = 1.5f;
sparkleParams.minVelocity = Vector3(-2.0f, -2.0f, -2.0f);
sparkleParams.maxVelocity = Vector3(2.0f, 2.0f, 2.0f);
sparkleParams.gravity = Vector3(0.0f, -5.0f, 0.0f); // Fall down
sparkleParams.startSize = 0.2f;
sparkleParams.endSize = 0.05f;
sparkleParams.colorOverLifetime = ColorCurve(
    Vector4(0.5f, 0.8f, 1.0f, 1.0f),  // Bright blue
    Vector4(1.0f, 1.0f, 1.0f, 0.0f)   // White, fade out
);
sparkleParams.materialName = "Particles/Additive.hmat";
```

## Future Enhancements

### Planned Features

1. **Advanced Color Curves**
   - Multi-point gradients
   - Custom interpolation modes (ease-in, ease-out)

2. **Sprite Sheet Animation**
   - Full implementation with frame progression
   - Random starting frames
   - Animation speed control

3. **Additional Forces**
   - Wind zones
   - Vortex forces
   - Attraction/repulsion points

4. **Collision Detection**
   - World collision
   - Bounce/stick/destroy behaviors

5. **Emitter Modules**
   - Sub-emitters (particles spawning particles)
   - Trails
   - Ribbons

6. **Performance Optimizations**
   - GPU particle simulation
   - Instanced rendering
   - LOD system

7. **Editor Enhancements**
   - Curve editor for color gradients
   - Preset library
   - Effect templates
   - Preview different materials in editor

## Conclusion

The MMO particle system provides a solid foundation for creating compelling visual effects. The combination of code API and visual editor makes it easy to iterate on effects and integrate them into your game.

For questions or issues, refer to the source code documentation or contact the development team.
