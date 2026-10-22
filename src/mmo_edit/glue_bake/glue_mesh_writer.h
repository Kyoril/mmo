// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "glue_scene_collector.h"

#include "base/typedefs.h"
#include "game_common/glue_bake.h"

#include <map>
#include <vector>

namespace mmo
{
	/// @brief What WriteMergedGlueMeshes produced.
	struct GlueMeshWriteStats
	{
		/// @brief Chunk cells that received merged geometry.
		uint32 chunks = 0;

		/// @brief Mesh files written (a chunk above the vertex budget writes several).
		uint32 files = 0;

		uint32 subMeshes = 0;
		uint64 vertices = 0;
		/// @brief Triangles written into the chunk meshes.
		uint64 triangles = 0;

		/// @brief Unmergeable items, referenced as their original mesh file.
		uint32 unmerged = 0;

		/// @brief Items merged into chunk meshes.
		uint32 merged = 0;

		/// @brief Mergeable items referenced as their original mesh file because their mesh is placed
		///        at least MinInstancedPlacements times (drawn instanced at runtime).
		uint32 instanced = 0;

		/// @brief Distinct meshes (path and material overrides) among the instanced items.
		uint32 instancedMeshes = 0;

		/// @brief Triangles of all instanced placements together.
		uint64 instancedTriangles = 0;

		/// @brief How often a mesh was merged, and roughly how many bytes it added.
		struct MeshUse
		{
			uint32 placements = 0;
			uint64 bytes = 0;
		};

		/// @brief Merged placements by mesh path.
		std::map<String, MeshUse> mergedByMesh;

		/// @brief Items dropped: non-finite transform or bounds, or a mesh that did not load.
		uint32 dropped = 0;

		/// @brief Items merged only in part because a submesh could not be read back.
		uint32 incomplete = 0;

		/// @brief Bytes of all mesh files written.
		uint64 bytes = 0;

		/// @brief Mesh files that could not be written.
		uint32 failedFiles = 0;
	};

	/// @brief Merges the glue render items into world-space chunk meshes and writes them.
	/// @details Items whose mesh (path and material overrides) is placed at least MinInstancedPlacements
	///          times, and items that cannot be merged, are returned as references to their original mesh
	///          file with their world transform: the runtime draws them as entities sharing the loaded
	///          mesh, so repeated props are not duplicated. The rest are grouped by chunk (the world XZ
	///          cell of their bounds' centre) and by material name (after overrides), merged into world
	///          space, and a material above MaxMergedSubMeshVertices continues in further files. Writes
	///          `<outDir>/Chunk_<x>_<z>_<n>.hmsh` (submeshes named "m<index>").
	/// @param items The collected render items.
	/// @param outDir Asset directory (written through the asset registry), or an absolute file system
	///        directory (written directly, for debugging); created as needed.
	/// @param chunkSize Edge length of a chunk cell in world units.
	/// @param stats Receives counts, if not null.
	/// @return Manifest entries: the chunk meshes with an identity transform, then the unmerged items.
	std::vector<GlueBakeMesh> WriteMergedGlueMeshes(const std::vector<GlueRenderItem>& items, const String& outDir, float chunkSize = 48.0f, GlueMeshWriteStats* stats = nullptr);
}
