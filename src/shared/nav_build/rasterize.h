// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

#include <vector>

class rcContext;
struct rcHeightfield;

namespace mmo
{
    /// @brief Rasterizes triangles into a Recast heightfield, marking each one walkable or not.
    ///
    /// A triangle is walkable when the angle between its normal and the vertical is at most
    /// `walkableSlope`, regardless of winding: terrain fans and imported models wind opposite ways.
    /// Degenerate triangles are skipped.
    ///
    /// Where a walkable and an unwalkable surface end at nearly the same height in one voxel column -
    /// a floor and the top edge of the wall, riser or platform side standing on it - the walkable one
    /// wins as long as their tops are within `flagMergeThreshold` voxels. Without that (a threshold of
    /// -1) whichever triangle is rasterized last decides, and every floor edge that meets a vertical
    /// face flush turns into a seam of unwalkable cells, which erosion then widens into a gap.
    ///
    /// @param ctx Recast context.
    /// @param heightField The heightfield to rasterize into.
    /// @param walkableSlope Maximum walkable slope in degrees.
    /// @param vertices Triangle vertices.
    /// @param indices Triangle list into vertices.
    /// @param areaFlags Area of walkable triangles (unwalkable ones get the null area).
    /// @param flagMergeThreshold Voxel distance within which span tops merge their areas; use the
    ///        config's walkableClimb.
    /// @return False if Recast failed.
    bool RasterizeNavTriangles(rcContext& ctx, rcHeightfield& heightField, float walkableSlope,
        const std::vector<Vector3>& vertices, const std::vector<int32>& indices, uint8 areaFlags, int flagMergeThreshold);
}
