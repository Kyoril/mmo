// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/collision_bake.h"
#include "math/collision_recipe.h"
#include "scene_graph/mesh.h"
#include "scene_graph/mesh_serializer.h"

#include <vector>

namespace mmo
{
	/// @brief Render triangles of a mesh's submeshes, gathered as collision input.
	struct RenderCollisionGeometry
	{
		/// @brief Vertex positions.
		std::vector<Vector3> vertices;
		/// @brief Triangle list.
		std::vector<uint32> indices;
		/// @brief Source submesh per face.
		std::vector<uint16> faceSubMeshes;
	};

	/// @brief Gathers the render triangles of the given submeshes (shared or per-submesh vertex data). Ids out of range are skipped.
	RenderCollisionGeometry GatherRenderCollisionGeometry(Mesh& mesh, const std::vector<uint16>& includedSubMeshes);

	/// @brief Ids of every submesh that has index data.
	std::vector<uint16> AllSubMeshesWithIndexData(Mesh& mesh);

	/// @brief Bakes the recipe into the mesh's collision tree.
	/// @param mesh Mesh whose tree is replaced.
	/// @param recipe Recipe; sanitized against the mesh in place.
	/// @param out_renderGeometry Receives the gathered render geometry, if not null (used by the overlay to draw cut faces).
	/// @param buildTree If false, only bakes (cheap preview during a drag) and leaves the tree untouched.
	/// @return The bake result.
	CollisionBakeResult RebuildMeshCollision(Mesh& mesh, CollisionRecipe& recipe, RenderCollisionGeometry* out_renderGeometry = nullptr, bool buildTree = true);

	/// @brief Reads the editor-only collision recipe from a mesh asset file.
	/// @return False if the file could not be opened.
	bool LoadMeshCollisionRecipe(const String& assetPath, CollisionRecipe& out_recipe, collision_recipe_read::Type& out_result);

	/// @brief Writes the mesh file at the latest version, with the recipe chunk if `recipe` is non-null.
	/// @return False if the file could not be created.
	bool SaveMeshWithRecipe(const MeshPtr& mesh, const String& assetPath, const CollisionRecipe* recipe);

	/// @brief Unattended `mmo_edit --rebake-collision`: loads a mesh, rebakes its stored recipe and saves it.
	/// @return False if the mesh has no (readable) recipe or could not be saved.
	bool RebakeMeshCollisionFile(const String& assetPath);
}
