// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <vector>

namespace mmo
{
	struct ParticleSystemParameters;

	/// @brief Manages user-savable particle presets stored as regular .hpar files
	///	       in the editor asset directory. Presets can contain one emitter or a
	///	       whole multi-emitter system and are directly openable in the editor.
	class ParticlePresetLibrary final
	{
	public:
		/// @brief Info about a single preset in the library.
		struct PresetInfo
		{
			/// @brief Display name of the preset (file name without folder and extension).
			String name;

			/// @brief Full asset registry path of the preset file.
			String path;
		};

	public:
		/// @brief Returns the list of available presets, sorted by name. Cached until invalidated.
		const std::vector<PresetInfo>& ListPresets();

		/// @brief Saves the given parameters as a named preset, overwriting an existing preset with the same name.
		/// @return true on success.
		bool SavePreset(const String& name, const ParticleSystemParameters& params);

		/// @brief Loads a preset file into the given parameter struct.
		/// @return true on success.
		bool LoadPreset(const String& path, ParticleSystemParameters& out) const;

		/// @brief Whether a preset with the given name already exists in the library.
		bool PresetExists(const String& name);

		/// @brief Forces a rescan of the preset folder on the next ListPresets() call.
		void Invalidate() { m_cacheValid = false; }

		/// @brief Turns an arbitrary display name into a safe file name (no path separators etc.).
		static String SanitizeName(const String& name);

	private:
		std::vector<PresetInfo> m_presets;
		bool m_cacheValid { false };
	};
}
