// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/collision_shape.h"

#include <vector>

namespace mmo
{
	/// @brief Collision geometry produced by BakeCollision, ready for AABBTree::Build.
	struct CollisionBakeResult
	{
		/// @brief Baked vertices.
		std::vector<Vector3> vertices;
		/// @brief Triangle list.
		std::vector<uint32> indices;
		/// @brief Submesh id per baked face.
		std::vector<uint16> faceSubMeshes;
		/// @brief Number of render faces removed by Cut shapes.
		uint32 cutFaces { 0 };
		/// @brief Render-face numbers (index into the input triangle list / 3) that were cut.
		std::vector<uint32> cutFaceIndices;
		/// @brief Per baked face: index of the Add shape that produced it, or -1 for a render face.
		std::vector<int32> faceShape;
	};

	/// @brief Bakes collision: render faces minus those whose centroid lies in a Cut shape, plus the Add shapes.
	/// @param renderVertices Render-derived collision vertices.
	/// @param renderIndices Render triangle list; faces with an out-of-range index are skipped.
	/// @param renderFaceSubMeshes Submesh per render face, or empty (then 0 is used).
	/// @param shapes Authored shapes; Cut acts on render faces only, never on other shapes.
	/// @return The baked geometry.
	CollisionBakeResult BakeCollision(const std::vector<Vector3>& renderVertices, const std::vector<uint32>& renderIndices,
		const std::vector<uint16>& renderFaceSubMeshes, const std::vector<CollisionShape>& shapes);
}
