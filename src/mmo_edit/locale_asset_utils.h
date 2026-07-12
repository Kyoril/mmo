// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

#include "assets/asset_registry.h"

namespace mmo
{
	/// @brief Strips a leading "Locales/Locale_<code>/" folder prefix from an asset path, if present.
	///
	/// The game client mounts the active locale folder (or its .hpak archive) as an asset root,
	/// so locale-based assets are addressed by their locale-relative path (e.g. "Voice/Foo.mp3")
	/// rather than their on-disk path ("Locales/Locale_enUS/Voice/Foo.mp3"). Asset references
	/// stored in game data must therefore use the locale-relative form, or they won't resolve
	/// in hpak release builds.
	///
	/// @param path The asset path to normalize (modified in place).
	/// @return true if a locale prefix was detected and stripped, false if the path was left untouched.
	inline bool NormalizeLocaleAssetPath(std::string& path)
	{
		static constexpr std::string_view s_localePrefix = "Locales/Locale_";

		if (path.size() <= s_localePrefix.size())
		{
			return false;
		}

		// Case-insensitive prefix match, tolerating backslash separators
		for (size_t i = 0; i < s_localePrefix.size(); ++i)
		{
			char c = path[i];
			if (c == '\\')
			{
				c = '/';
			}

			if (std::tolower(static_cast<unsigned char>(c)) != std::tolower(static_cast<unsigned char>(s_localePrefix[i])))
			{
				return false;
			}
		}

		// Strip everything up to and including the separator after the locale folder name
		const size_t separator = path.find_first_of("/\\", s_localePrefix.size());
		if (separator == std::string::npos || separator + 1 >= path.size())
		{
			return false;
		}

		path.erase(0, separator + 1);
		return true;
	}

	/// @brief Mounts every "Locales/Locale_<code>" folder below the given asset base path as its
	/// own asset registry root, mirroring how the game client mounts the active locale archive.
	/// This makes locale-based assets resolvable (and listable) under their locale-relative paths.
	///
	/// @param basePath The asset registry base path (the client data directory).
	inline void MountLocaleAssetArchives(const std::filesystem::path& basePath)
	{
		const auto localesPath = basePath / "Locales";

		std::error_code ec;
		if (!std::filesystem::is_directory(localesPath, ec))
		{
			return;
		}

		for (const auto& entry : std::filesystem::directory_iterator(localesPath, ec))
		{
			if (!entry.is_directory())
			{
				continue;
			}

			const auto folderName = entry.path().filename().string();
			if (!folderName.starts_with("Locale_"))
			{
				continue;
			}

			AssetRegistry::AddArchivePackage(std::filesystem::path("Locales") / folderName);
		}
	}
}
