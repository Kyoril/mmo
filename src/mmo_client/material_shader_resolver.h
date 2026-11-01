// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <filesystem>

namespace mmo
{
	/// @brief Installs how the client deals with material shaders that are missing for its platform.
	/// @remarks Development builds (MMO_MATERIAL_RUNTIME_COMPILE) check every loaded material's shaders
	///          against its graph and compile stale or missing ones, caching them under Cache/Shaders in
	///          the data directory; the repository's data is never written. Release builds never compile:
	///          drawing with a missing shader variant shows an error naming the material and exits.
	/// @param dataPath The client's data directory.
	void InstallMaterialShaderResolver(const std::filesystem::path& dataPath);

	/// @brief Removes what InstallMaterialShaderResolver installed.
	void UninstallMaterialShaderResolver();
}
