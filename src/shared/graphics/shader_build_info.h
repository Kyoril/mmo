// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <string_view>
#include <vector>

#include "base/sha1.h"
#include "base/typedefs.h"

namespace mmo
{
	/// @brief Content hash of one material function a shader was built from.
	struct FunctionHash
	{
		/// @brief Asset path of the material function.
		String path;

		/// @brief Hash of the material function's content.
		SHA1Hash hash{};
	};

	/// @brief Records what one platform's shaders of a material were built from, so stale
	///        shaders can be detected by comparing these values with the current ones.
	struct ShaderBuildInfo
	{
		/// @brief Hash of the material graph the shaders were built from.
		SHA1Hash graphHash{};

		/// @brief Version of the material compiler's generator (platform independent part).
		uint32 generatorVersion { 0 };

		/// @brief Version of the platform's shader backend.
		uint32 backendVersion { 0 };

		/// @brief Material functions used by the graph, kept sorted by path.
		std::vector<FunctionHash> functions;
	};

	namespace material_compiler_version
	{
		/// @brief Current version of the material compiler's generator. Bump when generated output changes.
		constexpr uint32 Generator = 1;

		/// @brief Gets the current backend version for a platform id (see shader_platform.h).
		/// @returns The version, or 0 for platforms without a material backend.
		uint32 GetBackendVersion(std::string_view platform);
	}
}
