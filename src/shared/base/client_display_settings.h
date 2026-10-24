// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "typedefs.h"
#include <array>
#include <string>
#include <string_view>

namespace mmo
{
	/// A supported game window size, shared by the launcher and client.
	struct ClientResolution
	{
		uint16 width;
		uint16 height;
		const char* label;
	};

	inline constexpr std::array<ClientResolution, 4> ClientResolutions =
	{{
		{ 1280, 720, "1280x720" },
		{ 1280, 960, "1280x960" },
		{ 1280, 1024, "1280x1024" },
		{ 1920, 1080, "1920x1080" }
	}};

	/// Returns a supported resolution, falling back to the default 1280x720.
	inline const ClientResolution& NormalizeClientResolution(std::string_view value)
	{
		for (const auto& resolution : ClientResolutions)
		{
			if (value == resolution.label)
			{
				return resolution;
			}
		}
		return ClientResolutions[0];
	}

	/// Persisted game display choices; a resolution is always selected.
	struct ClientDisplaySettings
	{
		std::string resolution = "1280x720";
		bool windowed = true;
	};
}
