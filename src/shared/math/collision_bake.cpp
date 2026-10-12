// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "collision_bake.h"

#include <limits>

namespace mmo
{
	CollisionBakeResult BakeCollision(const std::vector<Vector3>& renderVertices, const std::vector<uint32>& renderIndices,
		const std::vector<uint16>& renderFaceSubMeshes, const std::vector<CollisionShape>& shapes)
	{
		CollisionBakeResult result;

		std::vector<CollisionShape> cutShapes;
		for (const CollisionShape& shape : shapes)
		{
			const CollisionShape sane = SanitizeCollisionShape(shape);
			if (sane.op == collision_shape_op::Cut && CollisionShapeSupportsCut(sane.type))
			{
				cutShapes.push_back(sane);
			}
		}

		const size_t faceCount = renderIndices.size() / 3;
		const bool hasSubMeshes = renderFaceSubMeshes.size() == faceCount;
		constexpr uint32 unmapped = std::numeric_limits<uint32>::max();
		std::vector<uint32> remap(renderVertices.size(), unmapped);

		for (size_t face = 0; face < faceCount; ++face)
		{
			const uint32 corners[3] = { renderIndices[face * 3], renderIndices[face * 3 + 1], renderIndices[face * 3 + 2] };
			if (corners[0] >= renderVertices.size() || corners[1] >= renderVertices.size() || corners[2] >= renderVertices.size())
			{
				continue;
			}

			const Vector3 centroid = (renderVertices[corners[0]] + renderVertices[corners[1]] + renderVertices[corners[2]]) / 3.0f;
			bool cut = false;
			for (const CollisionShape& shape : cutShapes)
			{
				if (IsPointInsideCollisionShape(shape, centroid))
				{
					cut = true;
					break;
				}
			}

			if (cut)
			{
				result.cutFaceIndices.push_back(static_cast<uint32>(face));
				continue;
			}

			for (const uint32 corner : corners)
			{
				if (remap[corner] == unmapped)
				{
					remap[corner] = static_cast<uint32>(result.vertices.size());
					result.vertices.push_back(renderVertices[corner]);
				}
				result.indices.push_back(remap[corner]);
			}

			result.faceSubMeshes.push_back(hasSubMeshes ? renderFaceSubMeshes[face] : 0);
			result.faceShape.push_back(-1);
		}

		result.cutFaces = static_cast<uint32>(result.cutFaceIndices.size());

		for (size_t i = 0; i < shapes.size(); ++i)
		{
			const CollisionShape sane = SanitizeCollisionShape(shapes[i]);
			if (sane.op != collision_shape_op::Add)
			{
				continue;
			}

			std::vector<Vector3> vertices;
			std::vector<uint32> indices;
			TessellateCollisionShape(sane, vertices, indices);

			const uint32 base = static_cast<uint32>(result.vertices.size());
			result.vertices.insert(result.vertices.end(), vertices.begin(), vertices.end());
			for (const uint32 index : indices)
			{
				result.indices.push_back(base + index);
			}

			const size_t faces = indices.size() / 3;
			result.faceSubMeshes.insert(result.faceSubMeshes.end(), faces, sane.surfaceSubMesh);
			result.faceShape.insert(result.faceShape.end(), faces, static_cast<int32>(i));
		}

		return result;
	}
}
