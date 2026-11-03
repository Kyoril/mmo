// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <filesystem>
#include <string_view>

#include "base/typedefs.h"
#include "graphics/shader_build_info.h"

namespace mmo
{
	class Material;

	/// @brief Local cache of material shaders a development client compiled at load time, so it doesn't
	///        compile them again on the next start. One file per material and platform, holding a
	///        shader pack (see material_shader_pack.h). Never written into the repository's data.
	class MaterialShaderCache final
	{
	public:
		/// @brief Creates a cache in a directory, which is created on the first store.
		/// @param root The cache directory, usually Cache/Shaders under the data directory.
		explicit MaterialShaderCache(std::filesystem::path root);

	public:
		/// @brief Loads a material's cached shaders of a platform if they were built from the expected content.
		/// @param materialPath The material's asset path.
		/// @param platform The shader platform.
		/// @param expected The build info the shaders have to match (graph hash, versions, function hashes).
		/// @param into The material whose platform shaders and build info are replaced on a hit.
		/// @return true on a hit.
		bool TryLoad(const String& materialPath, std::string_view platform, const ShaderBuildInfo& expected, Material& into) const;

		/// @brief Stores a material's shaders of a platform. Failing to write is logged as a warning.
		/// @param materialPath The material's asset path.
		/// @param platform The shader platform.
		/// @param from The material, which must carry build info for the platform.
		void Store(const String& materialPath, std::string_view platform, const Material& from) const;

		/// @brief Gets the file a material's shaders of a platform are cached in. The name is the hex
		///        SHA1 of the material path and the platform, which keeps equal file names in different
		///        folders apart.
		/// @param materialPath The material's asset path.
		/// @param platform The shader platform.
		/// @return The cache file path.
		[[nodiscard]] std::filesystem::path GetFilePath(const String& materialPath, std::string_view platform) const;

	private:
		std::filesystem::path m_root;
	};
}
