# Foliage System Documentation

## Overview

The foliage system provides efficient rendering of grass, shrubs, rocks, and other vegetation using GPU instancing. It's designed to handle thousands of instances with minimal CPU overhead by batching instances into spatial chunks and rendering them with instanced draw calls.

## Architecture

### Core Components

1. **FoliageLayer** - Defines a single type of foliage (mesh + material + settings)
2. **FoliageChunk** - Manages a spatial region containing batched foliage instances
3. **Foliage** - Main system class that manages layers and chunks

### Class Hierarchy

```
Foliage (main system)
  └── FoliageLayer[] (foliage types)
  └── FoliageChunk[] (spatial batches)
        ├── MovableObject (scene integration)
        └── Renderable (rendering interface)
```

## Usage

### Basic Setup

```cpp
#include "scene_graph/foliage.h"

// Create foliage system
Foliage foliage(scene, graphicsDevice);

// Configure terrain height query callback
foliage.SetHeightQueryCallback([&terrain](float x, float z, float& height, Vector3& normal) {
    height = terrain.GetSmoothHeightAt(x, z);
    normal = terrain.GetSmoothNormalAt(x, z);
    return true;
});

// Set world bounds
foliage.SetBounds(AABB(
    Vector3(-500.0f, -100.0f, -500.0f),
    Vector3(500.0f, 200.0f, 500.0f)
));
```

### Adding Foliage Layers

```cpp
// Load a grass mesh
MeshPtr grassMesh = MeshManager::Get().Load("Models/Grass/grass_tuft.mesh");

// Create a layer with default settings
auto grassLayer = std::make_shared<FoliageLayer>("Grass", grassMesh);

// Configure layer settings
FoliageLayerSettings& settings = grassLayer->GetSettings();
settings.density = 2.0f;           // 2 instances per square unit
settings.minScale = 0.7f;          // Minimum random scale
settings.maxScale = 1.3f;          // Maximum random scale
settings.maxSlopeAngle = 30.0f;    // No grass on steep slopes
settings.fadeStartDistance = 40.0f;
settings.fadeEndDistance = 80.0f;
settings.castShadows = false;      // Disable shadows for performance

// Add to foliage system
foliage.AddLayer(grassLayer);
```

### Updating the Foliage System

Call `Update()` each frame with the current camera:

```cpp
void OnFrame(Camera& camera)
{
    foliage.Update(camera);
}
```

### System Settings

```cpp
FoliageSettings settings;
settings.chunkSize = 32.0f;              // Size of each chunk in world units
settings.maxViewDistance = 150.0f;       // Maximum view distance
settings.loadRadius = 5;                 // Chunks to load around camera
settings.frustumCulling = true;          // Enable frustum culling
settings.globalDensityMultiplier = 1.0f; // Global density scale

foliage.SetSettings(settings);
```

## Layer Settings Reference

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `density` | float | 1.0 | Instances per square unit |
| `minScale` | float | 0.8 | Minimum random scale |
| `maxScale` | float | 1.2 | Maximum random scale |
| `alignToNormal` | bool | false | Align to surface normal |
| `maxSlopeAngle` | float | 45.0 | Maximum slope in degrees |
| `randomYawRotation` | bool | true | Random Y-axis rotation |
| `minHeight` | float | -10000 | Minimum placement height |
| `maxHeight` | float | 10000 | Maximum placement height |
| `fadeStartDistance` | float | 50 | Distance fade begins |
| `fadeEndDistance` | float | 100 | Distance fully culled |
| `castShadows` | bool | false | Enable shadow casting |
| `randomSeed` | uint32 | 0 | RNG seed (0 = position-based) |

## Performance Considerations

### Density

Higher density values create more instances. For grass, typical values are:
- Low quality: 0.5 - 1.0
- Medium quality: 1.0 - 2.0
- High quality: 2.0 - 5.0

### Chunk Size

Smaller chunks provide better culling but more draw calls:
- Small scenes: 16 - 32 units
- Large scenes: 32 - 64 units

### View Distance

Balance between visual quality and performance:
- Grass: 50 - 100 units
- Larger vegetation: 100 - 200 units

## GPU Instancing

The system uses GPU instancing via `DrawIndexedInstanced` to render all instances in a chunk with a single draw call. Each instance has a 4x4 world transform matrix passed to the shader via an instance buffer.

### Shader Requirements

Foliage materials should support instanced rendering:

```hlsl
// Vertex shader input
struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    
    // Per-instance data (slot 1)
    float4 instanceWorld0 : TEXCOORD1;
    float4 instanceWorld1 : TEXCOORD2;
    float4 instanceWorld2 : TEXCOORD3;
    float4 instanceWorld3 : TEXCOORD4;
};

float4x4 GetInstanceWorldMatrix(VSInput input)
{
    return float4x4(
        input.instanceWorld0,
        input.instanceWorld1,
        input.instanceWorld2,
        input.instanceWorld3
    );
}
```

## Extending the System

### Custom Height Queries

The system uses a callback for terrain height queries, making it flexible for different terrain implementations:

```cpp
// For custom terrain
foliage.SetHeightQueryCallback([](float x, float z, float& height, Vector3& normal) {
    // Query your terrain system
    height = myCustomTerrain.GetHeight(x, z);
    normal = myCustomTerrain.GetNormal(x, z);
    return true;
});

// For entity-based placement (future feature)
foliage.SetHeightQueryCallback([](float x, float z, float& height, Vector3& normal) {
    // Ray cast against entities
    Ray ray(Vector3(x, 1000.0f, z), Vector3::NegativeUnitY);
    if (RaycastEntities(ray, height, normal))
    {
        return true;
    }
    return false;
});
```

### Multiple Layers

Combine different foliage types:

```cpp
// Grass layer
auto grass = std::make_shared<FoliageLayer>("Grass", grassMesh);
grass->GetSettings().density = 3.0f;
foliage.AddLayer(grass);

// Flowers layer (less dense)
auto flowers = std::make_shared<FoliageLayer>("Flowers", flowerMesh);
flowers->GetSettings().density = 0.5f;
foliage.AddLayer(flowers);

// Rocks layer (sparse)
auto rocks = std::make_shared<FoliageLayer>("Rocks", rockMesh);
rocks->GetSettings().density = 0.1f;
rocks->GetSettings().castShadows = true;
foliage.AddLayer(rocks);
```

## API Reference

### Foliage Class

```cpp
// Creation
Foliage(Scene& scene, GraphicsDevice& device);

// Layer management
void AddLayer(const FoliageLayerPtr& layer);
bool RemoveLayer(const String& name);
FoliageLayerPtr GetLayer(const String& name) const;
const std::vector<FoliageLayerPtr>& GetLayers() const;

// Configuration
void SetSettings(const FoliageSettings& settings);
void SetHeightQueryCallback(HeightQueryCallback callback);
void SetBounds(const AABB& bounds);
void SetVisible(bool visible);

// Updates
void Update(Camera& camera);
void RebuildAll();
void RebuildRegion(const AABB& region);

// Queries
size_t GetActiveChunkCount() const;
size_t GetTotalInstanceCount() const;
bool IsVisible() const;
```

### FoliageLayer Class

```cpp
// Creation
FoliageLayer(const String& name, MeshPtr mesh);
FoliageLayer(const String& name, MeshPtr mesh, MaterialPtr material);

// Configuration
void SetMesh(MeshPtr mesh);
void SetMaterial(MaterialPtr material);
void SetSettings(const FoliageLayerSettings& settings);
void SetDensity(float density);
void SetScaleRange(float minScale, float maxScale);
void SetHeightRange(float minHeight, float maxHeight);
void SetFadeDistances(float startDistance, float endDistance);

// Queries
const String& GetName() const;
const MeshPtr& GetMesh() const;
const MaterialPtr& GetMaterial() const;
const FoliageLayerSettings& GetSettings() const;
bool IsValidPlacement(const Vector3& position, float slopeAngle) const;
```
