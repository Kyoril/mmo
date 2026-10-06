// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "graphics_presets.h"

#include "console/console_var.h"

#include "base/signal.h"
#include "log/default_log_levels.h"

#include <algorithm>
#include <array>

namespace mmo
{
	namespace
	{
		using PresetTable = std::vector<std::pair<std::string, std::string>>;

		/// Every value must be spelled exactly like the matching item in the options screen
		/// (OptionsFrame.lua), so the dropdowns show the applied value instead of falling back.
		/// The steps were measured on an integrated Radeon 890M at 1600x900 (tools/perf): Medium is the
		/// preset that holds 60 fps there, Low leaves headroom for higher resolutions. The render scale
		/// is deliberately not part of any preset; it is the player's separate sharpness/speed choice.
		const std::array<PresetTable, GraphicsPresets::PresetCount> s_presets =
		{
			// Low
			PresetTable{
				{ "RenderShadows", "1" },
				{ "ShadowTextureSize", "1" },
				{ "ShadowQuality", "0" },
				{ "gxShadowDistance", "100" },
				{ "gxContactShadows", "0" },
				{ "gxContactShadowQuality", "0" },
				{ "gxSsao", "1" },
				{ "gxSsaoQuality", "0" },
				{ "gxSsaoHalfRes", "1" },
				{ "gxAtmosphereQuality", "1" },
				{ "gxBloomQuality", "1" },
				{ "ViewDistance", "250" },
				{ "FoliageEnabled", "1" },
				{ "FoliageDensity", "0.25" },
				{ "TerrainFarRadius", "2" },
				{ "gxAnisotropy", "2" },
				{ "gxDepthPrepass", "1" },
			},
			// Medium
			PresetTable{
				{ "RenderShadows", "1" },
				{ "ShadowTextureSize", "1" },
				{ "ShadowQuality", "1" },
				{ "gxShadowDistance", "150" },
				{ "gxContactShadows", "1" },
				{ "gxContactShadowQuality", "0" },
				{ "gxSsao", "1" },
				{ "gxSsaoQuality", "1" },
				{ "gxSsaoHalfRes", "1" },
				{ "gxAtmosphereQuality", "2" },
				{ "gxBloomQuality", "1" },
				{ "ViewDistance", "400" },
				{ "FoliageEnabled", "1" },
				{ "FoliageDensity", "0.5" },
				{ "TerrainFarRadius", "3" },
				{ "gxAnisotropy", "4" },
				{ "gxDepthPrepass", "1" },
			},
			// High
			PresetTable{
				{ "RenderShadows", "1" },
				{ "ShadowTextureSize", "2" },
				{ "ShadowQuality", "2" },
				{ "gxShadowDistance", "250" },
				{ "gxContactShadows", "1" },
				{ "gxContactShadowQuality", "1" },
				{ "gxSsao", "1" },
				{ "gxSsaoQuality", "2" },
				{ "gxSsaoHalfRes", "1" },
				{ "gxAtmosphereQuality", "3" },
				{ "gxBloomQuality", "2" },
				{ "ViewDistance", "600" },
				{ "FoliageEnabled", "1" },
				{ "FoliageDensity", "0.75" },
				{ "TerrainFarRadius", "4" },
				{ "gxAnisotropy", "8" },
				{ "gxDepthPrepass", "1" },
			},
			// Ultra
			PresetTable{
				{ "RenderShadows", "1" },
				{ "ShadowTextureSize", "3" },
				{ "ShadowQuality", "2" },
				{ "gxShadowDistance", "400" },
				{ "gxContactShadows", "1" },
				{ "gxContactShadowQuality", "2" },
				{ "gxSsao", "1" },
				{ "gxSsaoQuality", "2" },
				{ "gxSsaoHalfRes", "0" },
				{ "gxAtmosphereQuality", "4" },
				{ "gxBloomQuality", "2" },
				{ "ViewDistance", "100000" },
				{ "FoliageEnabled", "1" },
				{ "FoliageDensity", "1.0" },
				{ "TerrainFarRadius", "6" },
				{ "gxAnisotropy", "16" },
				{ "gxDepthPrepass", "1" },
			},
		};

		ConsoleVar* s_qualityVar = nullptr;
		scoped_connection s_qualityChanged;

		/// Parses a preset index, or returns -1 for "custom" and anything else that is not one.
		int ParsePreset(const std::string& value)
		{
			if (value.size() == 1 && value[0] >= '0' && value[0] < '0' + GraphicsPresets::PresetCount)
			{
				return value[0] - '0';
			}

			return -1;
		}
	}

	void GraphicsPresets::Initialize()
	{
		s_qualityVar = ConsoleVarMgr::RegisterConsoleVar("gxQuality",
			"Overall graphics quality: 0 = Low, 1 = Medium, 2 = High, 3 = Ultra. Setting it adjusts all graphics settings; \"custom\" keeps them as they are.",
			"custom");

		s_qualityChanged = s_qualityVar->Changed.connect([](ConsoleVar& var, const std::string&)
		{
			ApplyPreset(ParsePreset(var.GetStringValue()));
		});
	}

	void GraphicsPresets::Destroy()
	{
		s_qualityChanged.disconnect();
		s_qualityVar = nullptr;
	}

	const std::vector<std::pair<std::string, std::string>>& GraphicsPresets::GetPresetValues(const int preset)
	{
		return s_presets[static_cast<size_t>(std::clamp(preset, 0, PresetCount - 1))];
	}

	void GraphicsPresets::ApplyPreset(const int preset)
	{
		if (preset < 0 || preset >= PresetCount)
		{
			return;
		}

		ILOG("Applying graphics quality preset " << preset);

		for (const auto& [name, value] : GetPresetValues(preset))
		{
			// Settings owned by the world state only exist while in the world. Creating them here
			// is what the config file does as well: the owner picks up the value when it registers.
			if (ConsoleVar* var = ConsoleVarMgr::FindConsoleVar(name, true))
			{
				if (var->GetStringValue() != value)
				{
					var->Set(value);
				}
			}
			else
			{
				ConsoleVarMgr::RegisterConsoleVar(name, "", value);
			}
		}
	}
}
