// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// @brief Quality and debug settings of the atmosphere (fog + light shaft) pass.
	/// @remark Dependency-free so the headless deferred_shading_tests target can compile it.
	struct AtmospherePassSettings
	{
		/// @brief Upper bound of the march distance: the cascaded shadow map's maximum distance.
		static constexpr float MaxMarchDistance = 300.0f;

		/// @brief Current preset: 0 Off (analytic fog only), 1 Low, 2 Medium, 3 High, 4 Ultra.
		int32 qualityLevel = 3;

		/// @brief March target resolution = G-Buffer resolution / this.
		uint32 resolutionDivisor = 2;

		/// @brief Shadow-map steps per pixel. 0 disables the march entirely.
		uint32 stepCount = 48;

		/// @brief Number of separable bilateral blur iterations on the march target.
		uint32 blurIterations = 2;

		/// @brief Metres of each view ray that are ray-marched; the rest uses the closed form.
		float marchDistance = 200.0f;

		/// @brief Distance at which sky pixels (no geometry) are fogged.
		float skyDistance = 2000.0f;

		/// @brief 0 off, 1 in-scatter only, 2 transmittance, 3 march shadow term.
		uint32 debugMode = 0;

		/// @brief Applies a quality preset. Out-of-range values clamp.
		void ApplyQualityLevel(int level)
		{
			level = level < 0 ? 0 : (level > 4 ? 4 : level);
			qualityLevel = level;

			switch (level)
			{
			case 0:
				resolutionDivisor = 1;
				stepCount = 0;
				blurIterations = 0;
				break;
			case 1:
				resolutionDivisor = 4;
				stepCount = 12;
				blurIterations = 1;
				break;
			case 2:
				resolutionDivisor = 2;
				stepCount = 24;
				blurIterations = 1;
				break;
			case 3:
				resolutionDivisor = 2;
				stepCount = 48;
				blurIterations = 2;
				break;
			default:
				resolutionDivisor = 1;
				stepCount = 64;
				blurIterations = 2;
				break;
			}
		}

		/// @brief Sets the march distance, clamped to [0, MaxMarchDistance].
		void SetMarchDistance(const float value)
		{
			marchDistance = value < 0.0f ? 0.0f : (value > MaxMarchDistance ? MaxMarchDistance : value);
		}

		/// @brief Sets the debug view, clamped to [0, 3].
		void SetDebugMode(const int mode)
		{
			debugMode = static_cast<uint32>(mode < 0 ? 0 : (mode > 3 ? 3 : mode));
		}

		/// @brief Whether the shadowed march runs at all.
		[[nodiscard]] bool IsMarchEnabled() const { return stepCount > 0 && marchDistance > 0.0f; }
	};
}
