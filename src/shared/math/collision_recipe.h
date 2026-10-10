// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/collision_shape.h"

#include "binary_io/reader.h"
#include "binary_io/writer.h"

#include <vector>

namespace mmo
{
	/// @brief Version of the serialized collision recipe; readers reject any other value.
	constexpr uint32 CollisionRecipeVersion = 1;

	/// @brief Editor-only description of how a mesh's collision tree is built.
	struct CollisionRecipe
	{
		/// @brief Whether the render triangles of the included submeshes feed the collision.
		bool useRenderGeometry { true };
		/// @brief Submeshes whose render triangles are collidable, sorted and unique.
		std::vector<uint16> includedSubMeshes;
		/// @brief Authored shapes. Cut shapes act on render triangles only, Add shapes are appended.
		std::vector<CollisionShape> shapes;
	};

	/// @brief Writes a recipe, prefixed with CollisionRecipeVersion.
	io::Writer& operator<<(io::Writer& writer, const CollisionRecipe& recipe);

	/// @brief Reads a recipe; sets the reader's failure flag on an unknown version or invalid enum value.
	io::Reader& operator>>(io::Reader& reader, CollisionRecipe& recipe);

	/// @brief Sorted, distinct submesh ids of a tree's per-face submesh mapping.
	std::vector<uint16> InferIncludedSubMeshes(const std::vector<uint16>& faceSubMeshes);

	/// @brief Drops included ids >= subMeshCount, sorts and dedupes them, clamps each shape's
	///        surfaceSubMesh into range (0 if the mesh has no submeshes) and sanitizes every shape.
	void SanitizeCollisionRecipe(CollisionRecipe& recipe, uint16 subMeshCount);
}
