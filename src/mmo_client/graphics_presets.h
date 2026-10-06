// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include <string>
#include <utility>
#include <vector>

namespace mmo
{
	/// @brief Overall graphics quality presets, driven by the gxQuality console variable.
	/// @details Setting gxQuality to 0 (Low), 1 (Medium), 2 (High) or 3 (Ultra) assigns every
	///          setting the preset covers, through the normal console variables, so each one still
	///          reacts exactly as if it had been changed on its own. Any other value ("custom") leaves
	///          the individual settings alone; the options screen switches to it when the player edits
	///          one of the individual settings.
	///
	///          A preset is only applied when gxQuality changes, never while the config is loaded, so
	///          saved individual values always win at startup.
	class GraphicsPresets final : public NonCopyable
	{
	public:
		/// Number of presets (Low, Medium, High, Ultra).
		static constexpr int PresetCount = 4;

		/// @brief Registers gxQuality and starts listening for changes.
		static void Initialize();

		/// @brief Stops listening for changes.
		static void Destroy();

		/// @brief Returns the settings of a preset as (cvar, value) pairs.
		/// @param preset 0 = Low ... 3 = Ultra.
		[[nodiscard]] static const std::vector<std::pair<std::string, std::string>>& GetPresetValues(int preset);

		/// @brief Assigns every setting of a preset.
		/// @param preset 0 = Low ... 3 = Ultra. Other values are ignored.
		static void ApplyPreset(int preset);
	};
}
