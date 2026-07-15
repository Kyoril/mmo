// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// @brief Tunable settings for the screen-space ambient occlusion pass.
	/// @remark Deliberately free of any graphics dependency so it can be compiled into the
	///         headless unit_tests target. All distances are in world units (1 unit = 1 metre).
	struct SsaoSettings
	{
		/// @brief Whether the SSAO pass runs at all. When false the pass allocates no render
		///        targets and yields a 1x1 white texture instead.
		bool enabled = true;

		/// @brief Whether AO is computed at half the G-Buffer resolution and upsampled.
		bool halfResolution = true;

		/// @brief Whether to output the raw AO term to the screen for inspection.
		bool debugVisualization = false;

		/// @brief Sampling radius in metres.
		/// @remark Default 0.75m. The binding constraint is sample density, not visual reach:
		///         with only 4-12 steps per slice and no temporal accumulation, a larger radius
		///         spreads samples thin and bands. Raise stepCount alongside this.
		float radius = 0.75f;

		/// @brief Strength multiplier applied to the AO term as an exponent.
		float intensity = 1.0f;

		/// @brief Assumed occluder depth in metres.
		/// @remark Default 0.25m, roughly a player torso's depth. Too small and solid geometry
		///         stops occluding, leaking light through walls; too large and the heightfield
		///         over-darkening this technique exists to avoid comes back.
		float thickness = 0.25f;

		/// @brief Number of hemisphere slices marched per pixel. Driven by the quality preset.
		uint32 sliceCount = 4;

		/// @brief Number of steps marched per slice direction. Driven by the quality preset.
		uint32 stepCount = 12;

		/// @brief Applies a coarse quality preset trading visual quality for performance.
		/// @param level 0 = Low, 1 = Medium, 2 = High. Out-of-range values clamp.
		void ApplyQualityLevel(int level)
		{
			if (level <= 0)
			{
				sliceCount = 2;
				stepCount = 4;
			}
			else if (level == 1)
			{
				sliceCount = 3;
				stepCount = 8;
			}
			else
			{
				sliceCount = 4;
				stepCount = 12;
			}
		}

		/// @brief Sets the sampling radius in metres, clamped to a sane range.
		void SetRadius(float value)
		{
			radius = Clamp(value, 0.05f, 4.0f);
		}

		/// @brief Sets the strength multiplier, clamped to a sane range.
		void SetIntensity(float value)
		{
			intensity = Clamp(value, 0.1f, 8.0f);
		}

		/// @brief Sets the assumed occluder depth in metres, clamped to a sane range.
		void SetThickness(float value)
		{
			thickness = Clamp(value, 0.01f, 4.0f);
		}

	private:
		/// @brief Local clamp helper. Deliberately not pulling in a math header to keep this
		///        struct dependency-free for the headless unit_tests build.
		static float Clamp(float value, float minValue, float maxValue)
		{
			if (value < minValue)
			{
				return minValue;
			}

			if (value > maxValue)
			{
				return maxValue;
			}

			return value;
		}
	};
}
