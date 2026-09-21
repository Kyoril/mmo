// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "math/vector3.h"
#include "math/vector4.h"
#include "scene_graph/atmosphere_settings.h"
#include "scene_graph/environment_profile.h"

namespace mmo
{
	/// @brief Everything the sky, the lights, the fog and the post-process chain need for one frame.
	struct EnvironmentState
	{
		/// @brief Sky material horizon colour.
		Vector4 skyHorizon{ 0.5f, 0.7f, 1.0f, 1.0f };

		/// @brief Sky material zenith colour.
		Vector4 skyZenith{ 0.1f, 0.3f, 0.9f, 1.0f };

		/// @brief Sky material cloud colour.
		Vector4 clouds{ 1.0f, 1.0f, 1.0f, 1.0f };

		/// @brief Scene ambient colour.
		Vector3 ambient{ 0.03f, 0.04f, 0.06f };

		/// @brief Sun light colour.
		Vector3 sunColor{ 1.0f, 0.95f, 0.9f };

		/// @brief Sun light intensity.
		float sunIntensity = 1.0f;

		/// @brief Moon light colour.
		Vector3 moonColor{ 0.3f, 0.4f, 0.65f };

		/// @brief Moon light intensity.
		float moonIntensity = 0.12f;

		/// @brief Base fog and shaft values.
		AtmosphereParameters atmosphere;

		/// @brief Time-of-day fog tint, density multiplier, shaft tint and shaft multiplier.
		AtmosphereTimeOfDay timeOfDay;

		/// @brief Exposure before tone mapping (the player's brightness is applied on top).
		float exposure = 1.0f;

		/// @brief Bloom weight.
		float bloomIntensity = 0.08f;

		/// @brief Bloom threshold.
		float bloomThreshold = 0.8f;

		/// @brief Unit xz direction the wind blows toward (y = 0).
		Vector3 windDirection{ 0.70710678f, 0.0f, 0.70710678f };

		/// @brief Wind speed in m/s before gusts.
		float windSpeed = 3.0f;

		/// @brief Gust strength, [0, 1].
		float windGustiness = 0.3f;

		/// @brief Fog patchiness: 0 smooth, 1 very patchy.
		float fogNoiseAmount = 0.5f;

		/// @brief Metres per repeat of the fog noise.
		float fogNoiseSize = 60.0f;

		/// @brief Multiplier on point and spot light scattering in the fog.
		float lightScattering = 1.0f;

		/// @brief Texture path of the target profile's LUT. Empty = no colour grading.
		String colorLut;

		/// @brief LUT being faded out: the strongest other blending profile's LUT, or empty.
		String colorLutFrom;

		/// @brief Weight of colorLut; colorLutFrom (when not empty) gets 1 - colorLutBlend.
		float colorLutBlend = 1.0f;

		/// @brief Saturation applied with the LUT: 0 = grey, 1 = unchanged.
		float saturation = 1.0f;

		/// @brief Contrast around mid-grey applied with the LUT, 1 = unchanged.
		float contrast = 1.0f;

		/// @brief Multiplies the graded colour, 1 = unchanged.
		Vector3 colorFilter { 1.0f, 1.0f, 1.0f };
	};

	/// @brief Samples a profile at a time of day.
	/// @param profile The profile.
	/// @param normalizedTime 0 = midnight, 0.5 = noon.
	[[nodiscard]] EnvironmentState EvaluateEnvironment(const EnvironmentProfile& profile, float normalizedTime);

	/// @brief Component-wise linear blend. t = 0 returns a, t = 1 returns b.
	[[nodiscard]] EnvironmentState LerpEnvironment(const EnvironmentState& a, const EnvironmentState& b, float t);

	/// @brief Unit xz vector for a wind direction in degrees clockwise from +Z (0 = +Z, 90 = +X).
	[[nodiscard]] Vector3 WindDirectionFromDegrees(float degrees);
}
