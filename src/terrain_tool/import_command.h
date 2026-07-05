// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <filesystem>
#include <limits>

namespace mmo
{
	/// @brief Arguments of the import subcommand.
	struct ImportArgs
	{
		/// @brief Optional world name override (defaults to the world from the metadata file).
		String world;

		/// @brief Path of the 16-bit grayscale heightmap PNG.
		std::filesystem::path heightmapPath;

		/// @brief Path of the zone metadata JSON sidecar.
		std::filesystem::path metaPath;

		/// @brief Optional per-tile material asset path override.
		String materialOverride;

		/// @brief Optional water level override (NaN = use the value from the metadata file, if any).
		float waterLevelOverride = std::numeric_limits<float>::quiet_NaN();

		/// @brief Optional water material override.
		String waterMaterialOverride;
	};

	/// @brief Converts a zone heightmap image into terrain page (.tile) files.
	///	@param args The import arguments.
	///	@return Process exit code (0 on success).
	int32 RunImport(const ImportArgs &args);
}
