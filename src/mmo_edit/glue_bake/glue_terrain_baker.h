// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game_common/glue_bake.h"
#include "paging/page.h"

#include <vector>

namespace mmo
{
	class WorldEditorInstance;

	/// @brief Edge length in texels of the albedo baked per page for far terrain tiles.
	constexpr uint32 GlueFarAlbedoSize = 512;

	/// @brief Parent material of the far tiles' material instances (the one FarTerrain uses).
	constexpr const char* GlueFarTerrainMaterial = "Models/AlbedoNormal_Opaque_Base.hmat";

	/// @brief Normal map of the far tiles' material instances (FarTerrain's flat normal map).
	constexpr const char* GlueFarTerrainNormalMap = "Textures/Character/BaseFlattenNormalMap.htex";

	/// @brief What BakeGlueTerrain produced.
	struct GlueTerrainBakeStats
	{
		/// @brief Pages that had at least one visible tile.
		uint32 pages = 0;

		/// @brief Visible tiles within the near distance of a camera pose (one mesh each).
		uint32 nearTiles = 0;

		/// @brief Visible tiles beyond the near distance (merged per page).
		uint32 farTiles = 0;

		/// @brief Pages with far tiles (one mesh, albedo texture and material instance each).
		uint32 farPages = 0;

		/// @brief Visible tiles without any geometry (every cell a hole).
		uint32 emptyTiles = 0;

		uint64 nearTriangles = 0;
		uint64 farTriangles = 0;
		uint64 vertices = 0;

		/// @brief GPU memory of the written textures (splatting maps and far albedo with mips).
		uint64 textureBytes = 0;

		/// @brief Water quads written, their triangles (both faces where the material is single-sided) and
		///        the water meshes (one per page with visible water).
		uint64 waterQuads = 0;
		uint64 waterTriangles = 0;
		uint32 waterMeshes = 0;

		/// @brief Bytes of all written files.
		uint64 fileBytes = 0;
		uint32 files = 0;
		uint32 failedFiles = 0;
	};

	/// @brief Bakes the glue cameras' visible terrain into meshes.
	/// @details A tile (16x16 per page) is visible when its bounds intersect the frusta.
	///          - Near tiles (bounds within nearDistance of any pose) keep the live look: one world-space mesh
	///            per tile at full resolution with the live tile's vertices (heights, normals, vertex colours,
	///            texture coordinates) and holes, `<outDir>/NearTile_<px>_<pz>_<tx>_<tz>.hmsh`, drawn with the
	///            tile's own layered material; its layer coverage is written as
	///            `<outDir>/Splat_<px>_<pz>_<tx>_<tz>.htex` (uncompressed RGBA8, the exact texels terrain::Tile
	///            uploads) for the material's "Splatting" texture parameter.
	///          - Far tiles are merged per page into `<outDir>/FarTile_<px>_<pz>.hmsh`, decimated to stride 2,
	///            with full resolution edges towards near tiles and around holes, drawn with
	///            `<outDir>/FarAlbedo_<px>_<pz>.hmi`: an instance of GlueFarTerrainMaterial whose "Albedo" is the
	///            page's top-down colour `<outDir>/FarAlbedo_<px>_<pz>.htex` (GlueFarAlbedoSize², DXT1), set up
	///            like FarTerrain's material (flat normal map, specular 0.2, roughness 1). The colour is the
	///            unlit G-buffer albedo stored as is (no gamma), rendered without water.
	///          - Water quads of visible tiles become `<outDir>/Water_<px>_<pz>.hmsh` per page, built like the
	///            live water render object and drawn with the water profile's surface material.
	/// @param world The world editor instance holding the glue scene's world.
	/// @param pages The candidate terrain pages (GlueCollection::terrainPages).
	/// @param frusta The glue camera frusta.
	/// @param poses The glue camera poses.
	/// @param outDir Asset directory (written through the asset registry). An absolute file system directory
	///        is accepted for debugging, with an error logged: the files reference each other by that path.
	/// @param nearDistance Tiles closer than this to a camera pose are near tiles.
	/// @param stats Receives counts, if not null.
	/// @param water Receives the water meshes (identity transform), if not null; otherwise no water is written.
	/// @return One manifest entry per written mesh: near tiles with their material and splatting map, then the
	///         far pages with their material instance and no splatting map.
	std::vector<GlueBakeTerrainTile> BakeGlueTerrain(WorldEditorInstance& world, const std::vector<PagePosition>& pages, const GlueBakeFrustumSet& frusta,
		const std::vector<GlueCameraPose>& poses, const String& outDir, float nearDistance = 80.0f, GlueTerrainBakeStats* stats = nullptr, std::vector<GlueBakeMesh>* water = nullptr);
}
