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
	};

	/// @brief Samples a profile at a time of day.
	/// @param profile The profile.
	/// @param normalizedTime 0 = midnight, 0.5 = noon.
	[[nodiscard]] EnvironmentState EvaluateEnvironment(const EnvironmentProfile& profile, float normalizedTime);

	/// @brief Component-wise linear blend. t = 0 returns a, t = 1 returns b.
	[[nodiscard]] EnvironmentState LerpEnvironment(const EnvironmentState& a, const EnvironmentState& b, float t);
}
