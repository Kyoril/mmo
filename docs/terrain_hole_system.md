# Terrain Hole System Documentation

## Overview
The terrain hole system allows marking inner vertices as "holes" to create gaps in the terrain mesh. This is useful for creating features like caves, tunnels, or doorways in the terrain.

## Architecture

### Data Storage
- **Format**: Each tile stores its hole data as a single 64-bit unsigned integer (`uint64`)
- **Bit Mapping**: Each bit represents one of the 8x8 inner vertices in a tile
  - Bit index = `innerX + innerY * 8`
  - Value `1` = hole (vertex should not render triangles)
  - Value `0` = solid (normal rendering)
- **File Storage**: Holes are saved in a separate chunk (`MHLC` - Hole Chunk) in the page file
  - Only tiles with holes (non-zero hole maps) are written to save space
  - Default assumption: all vertices are solid unless specified otherwise

### File Format

#### Hole Chunk (MHLC)
```
uint16 holeCount                // Number of tiles with holes
For each tile with holes:
    uint16 tileIndex            // Index of tile in page (0-255)
    uint64 holeMap              // 64-bit hole bitmap
```

### Classes and Methods

#### Page Class
Located in [page.h](../src/shared/terrain/page.h) and [page.cpp](../src/shared/terrain/page.cpp)

**Data Members:**
- `std::vector<uint64> m_holeMap` - Hole map storage (one entry per tile in page)

**Methods:**
```cpp
// Check if an inner vertex is marked as a hole
bool IsHole(uint32 localTileX, uint32 localTileY, uint32 innerX, uint32 innerY) const;

// Set or clear the hole flag for an inner vertex
void SetHole(uint32 localTileX, uint32 localTileY, uint32 innerX, uint32 innerY, bool isHole);

// Get the entire hole map for a specific tile
uint64 GetTileHoleMap(uint32 localTileX, uint32 localTileY) const;
```

**Chunk Handlers:**
- `ReadMCHLChunk()` - Reads hole data from file
- Hole chunk is registered in `ReadMCVRChunk()` for version 2 files
- Hole data is written in `Save()` method (only for tiles with holes)

#### Tile Class
Located in [tile.h](../src/shared/terrain/tile.h) and [tile.cpp](../src/shared/terrain/tile.cpp)

**Modified Methods:**
- `CreateIndexData()` - Updated to skip triangles for vertices marked as holes
  - Queries hole map from parent page
  - Skips all 4 triangles surrounding a hole vertex

#### Terrain Class
Located in [terrain.h](../src/shared/terrain/terrain.h) and [terrain.cpp](../src/shared/terrain/terrain.cpp)

**New Methods:**
```cpp
// Paint holes in a circular brush area
void PaintHoles(float brushCenterX, float brushCenterZ, float radius, bool addHole);
```

**Parameters:**
- `brushCenterX`, `brushCenterZ` - World-space center of the brush
- `radius` - Radius of the circular brush in world units
- `addHole` - `true` to add holes, `false` to remove holes (make solid)

## Usage Example

### C++ Code
```cpp
// Get terrain instance
Terrain& terrain = GetTerrain();

// Add holes at world position (100, 50) with radius 10
terrain.PaintHoles(100.0f, 50.0f, 10.0f, true);

// Remove holes (make solid again)
terrain.PaintHoles(100.0f, 50.0f, 10.0f, false);

// Direct page-level access
Page* page = terrain.GetPage(0, 0);
if (page)
{
    // Mark inner vertex at tile (5,5), inner position (3,3) as hole
    page->SetHole(5, 5, 3, 3, true);
    
    // Check if vertex is a hole
    bool isHole = page->IsHole(5, 5, 3, 3);
    
    // Get entire tile hole map
    uint64 holeMap = page->GetTileHoleMap(5, 5);
}
```

### Editor Integration
The `PaintHoles()` method is designed to be called from the editor with brush parameters:

```cpp
// In editor tool handler
if (terrainHoleTool.IsActive())
{
    Vector3 brushPosition = GetBrushWorldPosition();
    float brushRadius = terrainHoleTool.GetBrushRadius();
    bool addingHoles = IsMouseButtonDown(LEFT);
    bool removingHoles = IsMouseButtonDown(RIGHT);
    
    if (addingHoles)
    {
        terrain.PaintHoles(brushPosition.x, brushPosition.z, brushRadius, true);
    }
    else if (removingHoles)
    {
        terrain.PaintHoles(brushPosition.x, brushPosition.z, brushRadius, false);
    }
}
```

## Implementation Details

### Index Buffer Generation
When a vertex is marked as a hole:
1. `Tile::CreateIndexData()` queries the hole map from the parent page
2. For each inner vertex, checks if the corresponding bit is set
3. If the vertex is a hole, skips adding the 4 triangles that use that vertex
4. Result: gaps appear in the terrain mesh at hole locations

### Automatic Updates
When holes are modified via `Page::SetHole()`:
- The page's changed flag is set (`m_changed = true`)
- The affected tile's index buffer is automatically regenerated
- Call to `tile->UpdateTerrain()` ensures visual update

### Memory Efficiency
- Each tile's hole data: 8 bytes (64 bits)
- Entire page (16x16 tiles): 2 KB
- File storage: Only non-zero hole maps are written
- For terrains with few/no holes: minimal file size impact

## Backward Compatibility
- Files without hole chunks default to all-solid (no holes)
- The hole chunk (`MHLC`) is optional in the page file format
- Legacy files (version 1) automatically have empty hole maps
- File version remains at 0x02 (v2 format)

## Limitations and Considerations

1. **Granularity**: Holes can only be placed at inner vertex positions (8x8 grid per tile)
2. **Collision**: Hole vertices still affect collision detection - separate collision mesh updates may be needed
3. **Navigation**: Navigation mesh generation should account for holes
4. **Physics**: Holes affect visual mesh only - physics/collision may need separate handling
5. **LOD**: Current implementation doesn't account for Level of Detail - may need updates for LOD support

## Future Enhancements

Potential improvements for the hole system:
1. **Undo/Redo Support**: Track hole changes for editor undo system
2. **Hole Visualization**: Special rendering mode to highlight holes in the editor
3. **Collision Integration**: Automatically update collision mesh when holes change
4. **Navigation Integration**: Auto-update navigation mesh for hole regions
5. **LOD Support**: Ensure holes work correctly with distance-based LOD
6. **Hole Smoothing**: Options to smooth or feather hole edges
7. **Brush Shapes**: Support for square/rectangular hole brushes in addition to circular

## Testing

To test the hole system:
```cpp
// Unit test example
void TestTerrainHoles()
{
    Terrain terrain(scene, camera, 1, 1);
    terrain.PreparePage(0, 0);
    terrain.LoadPage(0, 0);
    
    Page* page = terrain.GetPage(0, 0);
    ASSERT(page);
    
    // Initially no holes
    ASSERT(!page->IsHole(0, 0, 0, 0));
    
    // Add a hole
    page->SetHole(0, 0, 0, 0, true);
    ASSERT(page->IsHole(0, 0, 0, 0));
    
    // Remove the hole
    page->SetHole(0, 0, 0, 0, false);
    ASSERT(!page->IsHole(0, 0, 0, 0));
    
    // Test hole map
    page->SetHole(0, 0, 3, 4, true);  // Bit 35
    uint64 holeMap = page->GetTileHoleMap(0, 0);
    ASSERT(holeMap == (1ULL << 35));
}
```
