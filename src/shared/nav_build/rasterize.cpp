// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "rasterize.h"

#include "Recast.h"

#include <cmath>

namespace mmo
{
    bool RasterizeNavTriangles(rcContext& ctx, rcHeightfield& heightField, const float walkableSlope,
        const std::vector<Vector3>& vertices, const std::vector<int32>& indices, const uint8 areaFlags, const int flagMergeThreshold)
    {
        if (vertices.empty() || indices.empty())
        {
            return true;
        }

        std::vector<float> recastVertices;
        std::vector<int> cleanedIndices;
        std::vector<uint8> cleanedAreas;

        recastVertices.reserve(vertices.size() * 3);
        cleanedIndices.reserve(indices.size());
        cleanedAreas.reserve(indices.size() / 3);

        for (const Vector3& v : vertices)
        {
            recastVertices.push_back(v.x);
            recastVertices.push_back(v.y);
            recastVertices.push_back(v.z);
        }

        // Cosine of the maximum walkable slope. A triangle counts as walkable when the angle
        // between its face normal and the vertical axis is at or below this angle.
        const float walkableCosThreshold = std::cos(walkableSlope * (3.14159265358979323846f / 180.0f));

        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const Vector3& a = vertices[indices[i + 0]];
            const Vector3& b = vertices[indices[i + 1]];
            const Vector3& c = vertices[indices[i + 2]];

            const Vector3 normal = (b - a).Cross(c - a);
            const float normalLengthSq = normal.GetSquaredLength();

            // This is the critical degenerate triangle filter
            if (normalLengthSq < 1e-5f)
            {
                continue;
            }

            cleanedIndices.push_back(indices[i + 0]);
            cleanedIndices.push_back(indices[i + 1]);
            cleanedIndices.push_back(indices[i + 2]);

            // Slope test. We use the absolute value of the vertical normal component so the
            // result does not depend on triangle winding order - terrain fans wind the opposite
            // way from imported model geometry, and the previous rcClearUnwalkableTriangles call
            // (which assumes an upward-facing normal) rejected all terrain as a result. Steep
            // triangles get the null area so the near-vertical sides of trees, fences and walls
            // do not become walkable nav mesh.
            const float verticalCos = std::fabs(normal.y) / std::sqrt(normalLengthSq);
            cleanedAreas.push_back(verticalCos >= walkableCosThreshold ? areaFlags : static_cast<uint8>(0));
        }

        if (cleanedIndices.empty())
        {
            return true;
        }

        const int triangleCount = static_cast<int>(cleanedIndices.size() / 3);
        return rcRasterizeTriangles(
            &ctx, recastVertices.data(), static_cast<int>(vertices.size()), cleanedIndices.data(),
            cleanedAreas.data(), triangleCount, heightField, flagMergeThreshold);
    }
}
