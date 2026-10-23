// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "ocean_settings.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	/// @brief Converts a packed 0xAARRGGBB sRGB colour, as water profiles store them, to linear RGB.
	inline Vector3 UnpackOceanProfileColor(const uint32 packed)
	{
		const auto toLinear = [](const uint32 byte)
		{
			const float srgb = static_cast<float>(byte & 0xFFu) / 255.0f;
			return srgb <= 0.04045f ? srgb / 12.92f : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
		};

		return Vector3(toLinear(packed >> 16), toLinear(packed >> 8), toLinear(packed));
	}

	/// @brief Packs a linear RGB colour into the 0xAARRGGBB sRGB form water profiles store.
	inline uint32 PackOceanProfileColor(const Vector3& linear)
	{
		const auto toByte = [](const float value)
		{
			const float v = std::clamp(value, 0.0f, 1.0f);
			const float srgb = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
			return static_cast<uint32>(std::lround(srgb * 255.0f));
		};

		return 0xFF000000u | (toByte(linear.x) << 16) | (toByte(linear.y) << 8) | toByte(linear.z);
	}

	/// @brief Builds ocean settings from an authored water profile. Fields the profile leaves unset
	///        keep the engine defaults, so an empty profile renders the default ocean.
	/// @tparam TProfile The editor's proto::WaterProfile or the client's proto_client::WaterProfile,
	///         which mirror each other field for field (see ToWaterProfileValues).
	template <typename TProfile>
	[[nodiscard]] OceanSettings ToOceanSettings(const TProfile& profile)
	{
		OceanSettings settings;

		if (profile.has_ocean_wind_speed())
		{
			settings.windSpeed = std::clamp(profile.ocean_wind_speed(), 0.5f, 40.0f);
		}
		if (profile.has_ocean_wind_direction())
		{
			settings.windDirection = profile.ocean_wind_direction();
		}
		if (profile.has_ocean_fetch())
		{
			settings.fetch = std::clamp(profile.ocean_fetch(), 0.1f, 2000.0f);
		}
		if (profile.has_ocean_spread())
		{
			settings.spread = std::clamp(profile.ocean_spread(), 0.0f, 64.0f);
		}
		if (profile.has_ocean_wave_scale())
		{
			settings.waveScale = std::max(profile.ocean_wave_scale(), 0.0f);
		}
		if (profile.has_ocean_choppiness())
		{
			settings.choppiness = std::clamp(profile.ocean_choppiness(), 0.0f, 3.0f);
		}
		if (profile.has_ocean_wave_speed())
		{
			settings.timeScale = std::max(profile.ocean_wave_speed(), 0.0f);
		}
		if (profile.has_ocean_trough_color())
		{
			settings.troughColor = UnpackOceanProfileColor(profile.ocean_trough_color());
		}
		if (profile.has_ocean_crest_color())
		{
			settings.crestColor = UnpackOceanProfileColor(profile.ocean_crest_color());
		}
		if (profile.has_ocean_absorption_r())
		{
			settings.absorption.x = std::max(profile.ocean_absorption_r(), 0.0f);
		}
		if (profile.has_ocean_absorption_g())
		{
			settings.absorption.y = std::max(profile.ocean_absorption_g(), 0.0f);
		}
		if (profile.has_ocean_absorption_b())
		{
			settings.absorption.z = std::max(profile.ocean_absorption_b(), 0.0f);
		}
		if (profile.has_ocean_subsurface_strength())
		{
			settings.subsurfaceStrength = std::max(profile.ocean_subsurface_strength(), 0.0f);
		}
		if (profile.has_ocean_roughness())
		{
			settings.roughness = std::clamp(profile.ocean_roughness(), 0.0f, 1.0f);
		}
		if (profile.has_ocean_reflection_strength())
		{
			settings.reflectionStrength = std::max(profile.ocean_reflection_strength(), 0.0f);
		}
		if (profile.has_ocean_foam_coverage())
		{
			settings.foamCoverage = std::clamp(profile.ocean_foam_coverage(), -0.5f, 1.5f);
		}
		if (profile.has_ocean_foam_brightness())
		{
			settings.foamBrightness = std::max(profile.ocean_foam_brightness(), 0.0f);
		}
		if (profile.has_ocean_shore_foam_strength())
		{
			settings.shoreFoamStrength = std::max(profile.ocean_shore_foam_strength(), 0.0f);
		}

		return settings;
	}
}
