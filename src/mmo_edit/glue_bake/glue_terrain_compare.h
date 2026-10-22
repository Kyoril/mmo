// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game_common/glue_bake.h"
#include "paging/page.h"

#include <vector>

namespace mmo
{
	class WorldEditorInstance;

	/// @brief A camera view the terrain bake comparison renders.
	struct GlueTerrainCompareView
	{
		String name;
		GlueCameraPose pose;
	};

	/// @brief Renders the live terrain and the baked terrain tiles from each view and writes both images, so
	///        the bake can be checked against the live look (debug aid for the glue terrain bake).
	/// @details For each view writes `<artDir>/<name>_live.ppm` and `<name>_baked.ppm` (the lit frame) plus
	///          `<name>_live_albedo.ppm` and `<name>_baked_albedo.ppm` (the G-buffer albedo), and logs the mean
	///          and maximum albedo difference over pixels both renders cover. Only the terrain (live) or only the
	///          baked meshes are visible in the respective render.
	/// @param world The world editor instance holding the world.
	/// @param pages The terrain pages to load for the live render.
	/// @param tiles The baked tiles; their files must lie in the file system (an absolute output directory).
	/// @param views The views.
	/// @param artDir Absolute directory for the images (created as needed).
	/// @return False when a baked file could not be read or nothing could be rendered.
	bool CompareGlueTerrainBake(WorldEditorInstance& world, const std::vector<PagePosition>& pages, const std::vector<GlueBakeTerrainTile>& tiles,
		const std::vector<GlueTerrainCompareView>& views, const String& artDir);
}
