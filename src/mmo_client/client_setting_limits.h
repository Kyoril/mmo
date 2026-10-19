// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "math/clamp.h"

namespace mmo
{
	/// @brief Smallest camera distance the options UI offers for the MaxCameraZoom setting.
	constexpr float MinCameraZoomSetting = 2.0f;

	/// @brief Largest camera distance the options UI offers for the MaxCameraZoom setting.
	constexpr float MaxCameraZoomSetting = 15.0f;

	/// @brief Largest nameplate draw distance the options UI offers for the NameplateDistance setting.
	constexpr float MaxNameplateDistanceSetting = 60.0f;

	/// @brief Clamps a MaxCameraZoom value into the range the options UI allows.
	/// @param value Raw console variable value, which may come from a hand edited Config.cfg.
	/// @return The value limited to [MinCameraZoomSetting, MaxCameraZoomSetting].
	[[nodiscard]] inline float ClampMaxCameraZoom(const float value)
	{
		return Clamp<float>(value, MinCameraZoomSetting, MaxCameraZoomSetting);
	}

	/// @brief Clamps the current camera zoom against the sanctioned maximum camera distance.
	/// @param zoom Raw CameraZoom console variable value.
	/// @param maxZoom Raw MaxCameraZoom console variable value.
	/// @return The zoom limited to [0, ClampMaxCameraZoom(maxZoom)].
	[[nodiscard]] inline float ClampCameraZoom(const float zoom, const float maxZoom)
	{
		return Clamp<float>(zoom, 0.0f, ClampMaxCameraZoom(maxZoom));
	}

	/// @brief Clamps a NameplateDistance value into the range the options UI allows.
	/// @param distance Raw NameplateDistance console variable value.
	/// @return The distance limited to [0, MaxNameplateDistanceSetting].
	[[nodiscard]] inline float ClampNameplateDistance(const float distance)
	{
		return Clamp<float>(distance, 0.0f, MaxNameplateDistanceSetting);
	}
}
