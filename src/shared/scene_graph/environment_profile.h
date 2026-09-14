// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "graphics/color_curve.h"
#include "math/vector4.h"
#include "scene_graph/atmosphere_settings.h"

#include <initializer_list>
#include <memory>
#include <utility>

namespace mmo
{
	/// @brief Builds a colour curve with auto tangents from (time, colour) pairs.
	/// @param keys Keys over the normalized day, 0 = midnight, 0.5 = noon.
	/// @return The curve. An empty list yields an empty curve.
	[[nodiscard]] ColorCurve MakeEnvironmentCurve(std::initializer_list<std::pair<float, Vector4>> keys);

	/// @brief The lighting and mood of one zone over the day, ready to evaluate.
	/// @remark Every curve is always populated: loading fills unauthored curves from the Default.
	struct EnvironmentProfile
	{
		/// @brief Sky colour at the horizon.
		ColorCurve skyHorizon;

		/// @brief Sky colour overhead.
		ColorCurve skyZenith;

		/// @brief Cloud tint.
		ColorCurve clouds;

		/// @brief Scene ambient colour (rgb; alpha unused).
		ColorCurve ambient;

		/// @brief Sun colour (rgb) and intensity (a).
		ColorCurve sun;

		/// @brief Moon colour (rgb) and intensity (a).
		ColorCurve moon;

		/// @brief Fog tint (rgb) and density multiplier (a).
		ColorCurve fog;

		/// @brief Sun colour inside the fog (rgb) and light shaft multiplier (a).
		ColorCurve sunScatter;

		/// @brief Base fog and shaft values the fog and sunScatter alphas multiply.
		AtmosphereParameters atmosphere;

		/// @brief Linear exposure before tone mapping, [0.1, 8].
		float exposure = 1.0f;

		/// @brief Bloom weight, [0, 1].
		float bloomIntensity = 0.08f;

		/// @brief Bloom soft threshold, [0, 16].
		float bloomThreshold = 0.8f;

		/// @brief Seconds a fade INTO this profile takes. 0 snaps.
		float transitionSeconds = 3.0f;

		/// @brief Builds the built-in Default: the look the world had before profiles existed.
		[[nodiscard]] static EnvironmentProfile MakeDefault();

		/// @brief The shared, immutable built-in Default.
		[[nodiscard]] static const std::shared_ptr<const EnvironmentProfile>& GetDefault();
	};
}
