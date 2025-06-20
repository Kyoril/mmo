# Material-Based Render Queue Sorting

## Overview

This implementation adds material-based sorting to the render queue system to improve rendering performance by reducing GPU state changes. When objects with the same material are rendered consecutively, the GPU doesn't need to switch shaders, textures, or other render states as frequently.

## Performance Characteristics

### Sorting Algorithm
- **Algorithm**: `std::stable_sort` with material pointer comparison
- **Time Complexity**: O(n log n) where n is the number of renderables
- **Space Complexity**: O(1) - in-place sorting
- **Stability**: Preserves original order for objects with the same material

### Optimization Strategy
- **Threshold-based sorting**: Only sorts collections with >= N renderables (default: 8)
- **Fast comparison**: Uses raw pointer comparison (`material.get() < other.get()`)
- **Minimal overhead**: No material property comparisons or complex sorting keys

### When Sorting Provides Benefits
- Scenes with many objects sharing the same materials
- Complex materials with expensive state changes
- High polygon count objects where draw call optimization matters

### When to Avoid Sorting
- Very small scenes (< 8-20 renderables)
- Scenes where depth-based rendering order is critical for correctness
- Real-time scenarios where consistent frame times are more important than peak performance

## Usage Examples

### Basic Usage
```cpp
// After populating the render queue
renderQueue.SortByMaterial(); // Uses default threshold of 8 renderables

// Custom threshold
renderQueue.SortByMaterial(15); // Only sort groups with 15+ renderables
```

### Conditional Sorting
```cpp
// Only sort if the scene is complex enough to benefit
if (totalRenderables > 20 && uniqueMaterials < totalRenderables / 3)
{
    renderQueue.SortByMaterial(8);
}
```

### Per-Group Sorting
```cpp
// Sort specific render queue groups
auto* mainGroup = renderQueue.GetQueueGroup(RenderQueueGroupId::Main);
if (mainGroup)
{
    mainGroup->SortByMaterial(10);
}
```

## Implementation Details

### Architecture
The sorting functionality is implemented at multiple levels:

1. **QueuedRenderableCollection**: Core sorting implementation
2. **RenderPriorityGroup**: Sorts solid renderables within a priority group
3. **RenderQueueGroup**: Sorts all priority groups within a queue group  
4. **RenderQueue**: Sorts all groups and priorities in the entire queue

### Material Comparison
```cpp
// Fast pointer comparison for sorting
const auto materialA = a->GetMaterial();
const auto materialB = b->GetMaterial();
return materialA.get() < materialB.get();
```

This approach is extremely fast because:
- No virtual function calls during comparison
- No material property access
- Simple pointer arithmetic comparison
- Deterministic ordering based on memory addresses

### Null Material Handling
- Objects with null materials are sorted to the end
- Maintains stable ordering for null-material objects
- Prevents crashes from null pointer access

## Performance Guidelines

### Recommended Thresholds
- **Small scenes** (< 50 objects): threshold = 15-20
- **Medium scenes** (50-200 objects): threshold = 8-12  
- **Large scenes** (200+ objects): threshold = 5-8
- **Very large scenes** (1000+ objects): threshold = 3-5

### Measuring Effectiveness
Monitor these metrics to evaluate sorting effectiveness:
- GPU state change count (lower is better)
- Draw call batching efficiency
- Overall frame rendering time
- Material switch frequency

### Integration Points
Call `SortByMaterial()` at these points in your rendering pipeline:
1. After frustum culling but before rendering
2. After LOD selection but before draw call submission
3. In scene managers after object collection
4. In deferred rendering systems before G-buffer population

## Technical Notes

### Thread Safety
- The sorting operations are **not thread-safe**
- Call sorting methods only from the main rendering thread
- Ensure all renderables are added before sorting

### Memory Considerations
- In-place sorting minimizes memory allocations
- Material pointer access is cache-friendly
- Stable sort preserves spatial locality for same-material objects

### Limitations
- Does not consider render state beyond materials
- May break depth-dependent rendering for transparent objects
- Doesn't account for material parameter differences between instances

## Future Enhancements

Potential improvements for more advanced use cases:
- Multi-key sorting (material + depth + other criteria)
- Instanced rendering optimization
- Material parameter grouping
- Dynamic threshold adjustment based on scene characteristics
- GPU-driven sorting for very large scenes
