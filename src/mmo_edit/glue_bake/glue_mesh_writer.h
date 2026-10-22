// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "glue_scene_collector.h"

#include "base/typedefs.h"
#include "game_common/glue_bake.h"

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
		uint64 triangles = 0;

		/// @brief Items referenced as their original mesh file instead of being merged.
		uint32 unmerged = 0;

		/// @brief Items dropped: non-finite transform or bounds.
		uint32 dropped = 0;

		/// @brief Items merged only in part because a submesh could not be read back.
		uint32 incomplete = 0;

		/// @brief Bytes of all mesh files written.
		uint64 bytes = 0;

		/// @brief Mesh files that could not be written.
		uint32 failedFiles = 0;
	};

	/// @brief Merges the glue render items into world-space chunk meshes and writes them.
	/// @details Groups the mergeable items by chunk (the world XZ cell of their bounds' centre) and by
	///          material name (after overrides), merges them into world space and splits a material
	///          above MaxMergedSubMeshVertices into further files. Writes `<outDir>/Chunk_<x>_<z>_<n>.hmsh`
	///          (submeshes named "m<index>"). Items that cannot be merged are returned as references
	///          to their original mesh file with their world transform.
	/// @param items The collected render items.
	/// @param outDir Asset directory (written through the asset registry), or an absolute file system
	///        directory (written directly, for debugging); created as needed.
	/// @param chunkSize Edge length of a chunk cell in world units.
	/// @param stats Receives counts, if not null.
	/// @return Manifest entries: the chunk meshes with an identity transform, then the unmerged items.
	std::vector<GlueBakeMesh> WriteMergedGlueMeshes(const std::vector<GlueRenderItem>& items, const String& outDir, float chunkSize = 48.0f, GlueMeshWriteStats* stats = nullptr);
}
