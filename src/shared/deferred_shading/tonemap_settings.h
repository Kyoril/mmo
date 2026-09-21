// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "color_grading.h"

namespace mmo
{
	/// @brief Settings of the final tonemap pass.
	/// @remark Dependency-free so the headless deferred_shading_tests target can compile it.
	struct TonemapSettings
	{
		/// @brief Linear multiplier applied before ACES. 1 leaves the image unchanged.
		float exposure = 1.0f;

		/// @brief Dither amplitude in 8-bit steps (0.5 = ±half a step).
		float ditherStrength = 0.5f;

		/// @brief Parametric grade applied after gamma (saturation, contrast, colour filter).
		ColorGradingSettings grading;

		/// @brief Sets the exposure, clamped to [0.1, 8].
		void SetExposure(const float value)
		{
			exposure = value < 0.1f ? 0.1f : (value > 8.0f ? 8.0f : value);
		}
	};
}
