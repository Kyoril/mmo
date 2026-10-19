// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <algorithm>
#include <cmath>
#include <utility>

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

		/// @brief Minimum distance at which sky pixels (no geometry) are fogged; a camera whose far clip is
		///        farther fogs the sky at its far clip instead.
		static constexpr float SkyDistance = 2000.0f;

		/// @brief Default weight of the reprojected history in the temporal blend.
		static constexpr float DefaultHistoryWeight = 0.0f;

		/// @brief Largest allowed history weight. Every frame re-reads the history between texels, so a
		///        weight near 1 accumulates that resampling into a softer image while moving.
		static constexpr float MaxHistoryWeight = 0.95f;

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

		/// @brief Weight of the reprojected history in the temporal blend, [0, MaxHistoryWeight].
		float historyWeight = DefaultHistoryWeight;

		/// @brief Whether samples are jittered within their froxel from frame to frame. Only takes effect
		///        while the history is on; without history jitter is pure per-frame noise.
		bool jitterEnabled = true;

		/// @brief Diagnostic switches (gxFogDiag): 1 no depth culling, 2 no sun, 4 sun unshadowed, 8 constant density.
		uint32 diagFlags = 0;

		/// @brief History weight of the sun's scattering volume (0 = none). Needs historyWeight > 0 for the jitter.
		float sunHistoryWeight = 0.0f;

		/// @brief Largest sun shadow tap count along one screen axis of a froxel.
		static constexpr uint32 MaxSunTapsXY = 4;

		/// @brief Largest sun shadow tap count along a froxel's depth.
		static constexpr uint32 MaxSunTapsZ = 16;

		/// @brief Sun shadow taps along each screen axis of a froxel. The sun's visibility is box-filtered
		///        over the whole cell with a fixed pattern every frame and gets no history at all: a light
		///        shaft then depends only on where the cell is right now, so it can neither lag behind the
		///        camera nor flicker with a per-frame jitter.
		uint32 sunTapsXY = 2;

		/// @brief Sun shadow taps along a froxel's depth (a froxel is a needle several metres long at
		///        mid range; too few taps show as bands across the shafts).
		uint32 sunTapsZ = 6;

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
				sunTapsXY = 2;
				sunTapsZ = 4;
				break;
			case 2:
				tileSize = 16;
				sliceCount = 48;
				sunTapsXY = 2;
				sunTapsZ = 6;
				break;
			case 3:
				tileSize = 12;
				sliceCount = 64;
				sunTapsXY = 2;
				sunTapsZ = 6;
				break;
			default:
				tileSize = 8;
				sliceCount = 96;
				sunTapsXY = 2;
				sunTapsZ = 6;
				break;
			}
		}

		/// @brief Sets the sun shadow taps per froxel, clamped to [1, MaxSunTapsXY] and [1, MaxSunTapsZ].
		void SetSunTaps(const uint32 xy, const uint32 z)
		{
			sunTapsXY = std::clamp(xy, 1u, MaxSunTapsXY);
			sunTapsZ = std::clamp(z, 1u, MaxSunTapsZ);
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

		/// @brief Sets the temporal history weight, clamped to [0, MaxHistoryWeight]. 0 disables the history.
		void SetHistoryWeight(const float value)
		{
			historyWeight = std::clamp(value, 0.0f, MaxHistoryWeight);
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

		/// @brief Radical inverse of index in any base >= 2 (Halton sequence).
		[[nodiscard]] inline float Halton(uint32 index, const uint32 base)
		{
			float result = 0.0f;
			float fraction = 1.0f / static_cast<float>(base);
			while (index > 0)
			{
				result += static_cast<float>(index % base) * fraction;
				index /= base;
				fraction /= static_cast<float>(base);
			}

			return result;
		}

		/// @brief Depth jitter in [0, 1) for a frame number, repeating every JitterSequenceLength frames.
		[[nodiscard]] inline float JitterForFrame(const uint64 frame)
		{
			return HaltonBase2(static_cast<uint32>(frame % VolumetricFogSettings::JitterSequenceLength) + 1u);
		}

		/// @brief Screen-space jitter in [0, 1)^2 within a froxel for a frame number (Halton bases 3 and 5,
		///        so it does not correlate with the base-2 depth jitter). Jittering across the cell lets the
		///        history converge to the cell's average instead of its centre sample.
		[[nodiscard]] inline std::pair<float, float> ScreenJitterForFrame(const uint64 frame)
		{
			const uint32 index = static_cast<uint32>(frame % VolumetricFogSettings::JitterSequenceLength) + 1u;
			return { Halton(index, 3), Halton(index, 5) };
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
