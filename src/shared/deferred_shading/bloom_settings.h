// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// @brief Settings of the bloom pass.
	/// @remark Dependency-free so the headless deferred_shading_tests target can compile it.
	struct BloomSettings
	{
		/// @brief Current preset: 0 Off, 1 Low, 2 High.
		int32 qualityLevel = 2;

		/// @brief First downsample level = scene resolution / this.
		uint32 startDivisor = 2;

		/// @brief Number of downsample levels. 0 disables bloom.
		uint32 levelCount = 6;

		/// @brief Weight of the bloom when added to the scene.
		float intensity = 0.08f;

		/// @brief Linear brightness at which the soft threshold is centred.
		float threshold = 0.8f;

		/// @brief Width of the soft threshold knee.
		float knee = 0.5f;

		/// @brief Applies a quality preset. Out-of-range values clamp.
		void ApplyQualityLevel(int level)
		{
			level = level < 0 ? 0 : (level > 2 ? 2 : level);
			qualityLevel = level;

			if (level == 0)
			{
				levelCount = 0;
			}
			else if (level == 1)
			{
				startDivisor = 4;
				levelCount = 4;
			}
			else
			{
				startDivisor = 2;
				levelCount = 6;
			}
		}

		/// @brief Sets the intensity, clamped to [0, 1].
		void SetIntensity(const float value)
		{
			intensity = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
		}

		/// @brief Sets the threshold, clamped to [0, 16].
		void SetThreshold(const float value)
		{
			threshold = value < 0.0f ? 0.0f : (value > 16.0f ? 16.0f : value);
		}

		/// @brief Whether the bloom pass runs.
		[[nodiscard]] bool IsEnabled() const { return levelCount > 0; }
	};
}
