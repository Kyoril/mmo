// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <filesystem>
#include <vector>

#include "cook_report.h"

namespace mmo
{
	class ShaderCompiler;

	/// @brief Lists every .hmat below a data directory, sorted, as paths relative to it with forward
	///        slashes. The Cache directory (development client shader cache) is skipped.
	/// @param dataDir The data directory.
	/// @return The relative material paths.
	std::vector<String> ListMaterials(const std::filesystem::path& dataDir);

	/// @brief Gets the shader pack file name of a material and platform: "<relative path>.<platform>.shp".
	/// @param materialPath Material path relative to the data directory.
	/// @param platform Shader platform id.
	/// @return The pack path relative to a pack directory.
	std::filesystem::path GetShaderPackPath(const String& materialPath, std::string_view platform);

	/// @brief Builds one platform's shaders of every material in a data directory and writes them as
	///        shader packs (build info followed by the platform's shaders), plus warnings.json.
	/// @param dataDir The data directory. The asset registry must be initialized on it (material functions
	///        are read through it).
	/// @param compiler The shader compiler of the platform to cook.
	/// @param outDir Directory the packs and warnings.json are written to.
	/// @param report Receives a warning per material that can't be cooked and per failed variant.
	/// @return false only on I/O errors.
	bool CookShaders(const std::filesystem::path& dataDir, ShaderCompiler& compiler, const std::filesystem::path& outDir, CookReport& report);
}
