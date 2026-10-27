// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "graphics/vertex_format.h"
#include "math/matrix4.h"

#include <cmath>
#include <utility>
#include <vector>

namespace mmo
{
	/// @brief The one vertex layout every static mesh is loaded with, and therefore what merged world
	///        model geometry is built from.
	using WorldModelMergeVertex = POS_COL_NORMAL_BINORMAL_TANGENT_TEX_VERTEX;

	/// @brief Placements whose mesh bounding radius is at most this (in world model units) are
	///        "detail": small clutter that is merged into its own chunk, drawn only up to
	///        WorldModelDetailDistance and never casting shadows.
	constexpr float WorldModelDetailRadius = 1.5f;

	/// @brief Rendering distance of the detail chunk.
	constexpr float WorldModelDetailDistance = 70.0f;

	/// @brief Edge length of the square cells (on the XZ plane of the top-level world model) into which
	///        the outdoor geometry of a world model and all of its nested child world models is merged.
	/// @details Outdoor groups are only frustum culled, so merging them per group bought nothing over
	///          merging them per area, but cost one draw per group and material in every pass: a town of
	///          a hundred street cells and twenty houses drew a thousand times per pass, and again in
	///          every shadow cascade. Per cell, the draws are bounded by area and material count.
	constexpr float WorldModelClusterSize = 64.0f;

	/// @brief Cell of the cluster grid a top-level world model space position falls into.
	/// @param position The position, in the top-level world model's space.
	/// @return The cell's X and Z index.
	[[nodiscard]] inline std::pair<int32, int32> GetWorldModelClusterCell(const Vector3& position)
	{
		return {
			static_cast<int32>(std::floor(position.x / WorldModelClusterSize)),
			static_cast<int32>(std::floor(position.z / WorldModelClusterSize))
		};
	}

	/// @brief Centre of a cluster cell at height zero; the merged vertices of the cell are stored
	///        relative to it, and its scene node sits there, so distance culling measures from the cell.
	[[nodiscard]] inline Vector3 GetWorldModelClusterOrigin(const std::pair<int32, int32>& cell)
	{
		return Vector3(
			(static_cast<float>(cell.first) + 0.5f) * WorldModelClusterSize,
			0.0f,
			(static_cast<float>(cell.second) + 0.5f) * WorldModelClusterSize);
	}

	/// @brief Whether a placement counts as detail. @see WorldModelDetailRadius
	/// @param boundingRadius Bounding radius of the placed mesh, after the placement's scale.
	[[nodiscard]] inline bool IsWorldModelDetail(const float boundingRadius)
	{
		return boundingRadius <= WorldModelDetailRadius;
	}

	/// @brief Appends one mesh's geometry, transformed into world model space, to a merged buffer.
	/// @details Positions are transformed by the matrix, normals by its inverse transpose, tangents
	///          and binormals by the matrix itself (they follow the surface, which keeps them consistent
	///          with the texture coordinates), all renormalised. A transform with a negative determinant
	///          mirrors the mesh and reverses its winding; the triangles are then re-wound so the merged
	///          geometry needs no special face culling.
	/// @param vertices The mesh's vertices.
	/// @param vertexCount Number of vertices.
	/// @param indices The mesh's triangle list indices, relative to its own vertices.
	/// @param indexCount Number of indices; a multiple of three.
	/// @param transform Mesh space to world model space.
	/// @param outVertices Receives the transformed vertices, appended.
	/// @param outIndices Receives the indices, appended and offset to the appended vertices.
	void AppendTransformedGeometry(
		const WorldModelMergeVertex* vertices,
		size_t vertexCount,
		const uint32* indices,
		size_t indexCount,
		const Matrix4& transform,
		std::vector<WorldModelMergeVertex>& outVertices,
		std::vector<uint32>& outIndices);
}
