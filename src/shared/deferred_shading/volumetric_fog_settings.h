// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	/// @brief Quality and debug settings of the froxel volumetric fog pass.
	/// @remark Dependency-free so the headless deferred_shading_tests target can compile it.
	struct VolumetricFogSettings
	{
		/// @brief Smallest allowed fog grid range in metres.
		static constexpr float MinRange = 50.0f;

		/// @brief Largest allowed fog grid range: the cascaded shadow map's maximum distance.
		static constexpr float MaxRange = 300.0f;

		/// @brief View depth of the grid's near plane in metres.
		static constexpr float NearDistance = 0.5f;

		/// @brief Distance at which sky pixels (no geometry) are fogged.
		static constexpr float SkyDistance = 2000.0f;

		/// @brief Weight of the reprojected history in the temporal blend.
		static constexpr float TemporalBlend = 0.9f;

		/// @brief A camera jump longer than this within one frame discards the history.
		static constexpr float HistoryResetDistance = 50.0f;

		/// @brief Edge length of the tiling noise volume in texels.
		static constexpr uint32 NoiseResolution = 64;

		/// @brief Number of frames in the depth jitter sequence.
		static constexpr uint32 JitterSequenceLength = 16;

		/// @brief Current preset: 0 Off (analytic fog only), 1 Low, 2 Medium, 3 High, 4 Ultra.
		int32 qualityLevel = 3;

		/// @brief Screen pixels per grid cell along each screen axis. 0 disables the volume.
		uint32 tileSize = 12;

		/// @brief Number of exponential depth slices. 0 disables the volume.
		uint32 sliceCount = 64;

		/// @brief View depth covered by the grid in metres.
		float range = 200.0f;

		/// @brief 0 off, 1 scattered light, 2 transmittance, 3 density, 4 lights per fog block.
		uint32 debugMode = 0;

		/// @brief Multiplier on point and spot light scattering (the zone's light_scattering).
		float lightScatterStrength = 1.0f;

		/// @brief Applies a quality preset. Out-of-range values clamp.
		void ApplyQualityLevel(int level)
		{
			level = std::clamp(level, 0, 4);
			qualityLevel = level;

			switch (level)
			{
			case 0:
				tileSize = 0;
				sliceCount = 0;
				break;
			case 1:
				tileSize = 24;
				sliceCount = 32;
				break;
			case 2:
				tileSize = 16;
				sliceCount = 48;
				break;
			case 3:
				tileSize = 12;
				sliceCount = 64;
				break;
			default:
				tileSize = 8;
				sliceCount = 96;
				break;
			}
		}

		/// @brief Sets the grid range, clamped to [MinRange, MaxRange].
		void SetRange(const float value)
		{
			range = std::clamp(value, MinRange, MaxRange);
		}

		/// @brief Sets the debug view, clamped to [0, 4].
		void SetDebugMode(const int mode)
		{
			debugMode = static_cast<uint32>(std::clamp(mode, 0, 4));
		}

		/// @brief Sets the point and spot light scattering strength, clamped to [0, 8].
		void SetLightScatterStrength(const float value)
		{
			lightScatterStrength = std::clamp(value, 0.0f, 8.0f);
		}

		/// @brief Whether the froxel volume runs at all.
		[[nodiscard]] bool IsVolumeEnabled() const { return tileSize > 0 && sliceCount > 0; }

		/// @brief Grid cells across the screen width. Never 0.
		[[nodiscard]] uint32 GetGridWidth(const uint32 renderWidth) const
		{
			return tileSize == 0 ? 1u : std::max(1u, (renderWidth + tileSize - 1) / tileSize);
		}

		/// @brief Grid cells across the screen height. Never 0.
		[[nodiscard]] uint32 GetGridHeight(const uint32 renderHeight) const
		{
			return tileSize == 0 ? 1u : std::max(1u, (renderHeight + tileSize - 1) / tileSize);
		}
	};

	namespace volumetric_fog
	{
		/// @brief View depth of a normalized slice coordinate (0 = near plane, 1 = far plane), exponential.
		[[nodiscard]] inline float SliceToDepth(const float slice01, const float nearDistance, const float farDistance)
		{
			return nearDistance * std::pow(farDistance / nearDistance, slice01);
		}

		/// @brief Normalized slice coordinate of a view depth. 0 at or before the near plane; grows past 1
		///        beyond the far plane.
		[[nodiscard]] inline float DepthToSlice(const float depth, const float nearDistance, const float farDistance)
		{
			if (depth <= nearDistance)
			{
				return 0.0f;
			}

			return std::log(depth / nearDistance) / std::log(farDistance / nearDistance);
		}

		/// @brief Radical inverse of index in base 2 (Halton sequence). index 1 -> 0.5, 2 -> 0.25, 3 -> 0.75.
		[[nodiscard]] inline float HaltonBase2(uint32 index)
		{
			float result = 0.0f;
			float fraction = 0.5f;
			while (index > 0)
			{
				if ((index & 1u) != 0)
				{
					result += fraction;
				}

				index >>= 1;
				fraction *= 0.5f;
			}

			return result;
		}

		/// @brief Depth jitter in [0, 1) for a frame number, repeating every JitterSequenceLength frames.
		[[nodiscard]] inline float JitterForFrame(const uint64 frame)
		{
			return HaltonBase2(static_cast<uint32>(frame % VolumetricFogSettings::JitterSequenceLength) + 1u);
		}

		/// @brief Multiplier noise applies to the height-fog density. Averages 1 over uniformly distributed
		///        noise, so the fog beyond the grid (which has no noise) matches the grid's average.
		/// @param noise Noise sample in [0, 1].
		/// @param amount 0 smooth fog, 1 fully patchy.
		/// @remark Mirrored by NoiseDensityFactor in VolumetricFogCommon.hlsli.
		[[nodiscard]] inline float NoiseDensityFactor(const float noise, const float amount)
		{
			return std::max(0.0f, 1.0f + amount * (2.0f * noise - 1.0f));
		}

		/// @brief Texture-space z coordinate that samples the integrated volume at the end of the slice
		///        containing slice01.
		/// @param slice01 Normalized slice coordinate (see DepthToSlice), clamped internally to [0, 1].
		/// @param sliceCount Number of depth slices in the grid (VolumetricFogSettings::sliceCount). 0 returns 0.
		/// @remark Integrated texel z holds the accumulated value at the *end* of slice z, i.e. at slice
		///         coordinate (z + 1) / sliceCount, one texel-width ahead of the usual texel-centre
		///         convention SliceToDepth/DepthToSlice use. Mirrored by IntegratedTexelCoordinate in
		///         VolumetricFogCommon.hlsli, used by PS_FogComposite.
		[[nodiscard]] inline float IntegratedTexelCoordinate(const float slice01, const uint32 sliceCount)
		{
			if (sliceCount == 0)
			{
				return 0.0f;
			}

			return std::clamp((slice01 * static_cast<float>(sliceCount) - 0.5f) / static_cast<float>(sliceCount), 0.0f, 1.0f);
		}
	}
}
