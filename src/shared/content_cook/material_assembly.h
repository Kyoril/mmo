// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <filesystem>
#include <vector>

#include "cook_report.h"

namespace mmo
{
	/// @brief Writes every material that has at least one shader pack into an overlay directory, with the
	///        packs' platform shaders and build info replacing the repository's. Platforms without a pack
	///        keep the repository's shaders; the material graph chunk is appended unchanged. Also merges
	///        the packs' warnings.json files into the report and writes the overlay's overlay.json.
	/// @param dataDir The data directory the packs were cooked from.
	/// @param shaderPackDirs Directories written by CookShaders, one per platform.
	/// @param overlayDir The overlay directory to write.
	/// @param report Receives the packs' and the assembly's warnings.
	/// @return false only on I/O errors.
	bool AssembleMaterials(const std::filesystem::path& dataDir, const std::vector<std::filesystem::path>& shaderPackDirs, const std::filesystem::path& overlayDir, CookReport& report);
}
