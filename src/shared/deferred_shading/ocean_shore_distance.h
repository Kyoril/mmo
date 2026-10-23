// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace mmo
{
	/// @brief Turns the terrain's two-channel ocean mask into the four-channel texture the ocean
	///        shaders sample, adding the horizontal distance to the shore.
	///
	/// Shore foam driven by water depth alone collapses into a thin line on any steep beach (the
	/// water is metres deep a few metres out), so the foam bands follow the distance from the shore
	/// instead, as WoW's water does. Header-only and dependency-free for deferred_shading_tests.
	namespace ocean_shore
	{
		/// @brief Distance (m) that encodes as 255 in the blue channel. OceanRenderCommon.hlsli
		///        decodes with the same value.
		constexpr float DistanceRange = 32.0f;

		/// @brief Builds the shader mask: R = ocean present, G = depth (as the terrain encoded it),
		///        B = distance to the nearest shore texel / DistanceRange, A = 255.
		/// @param rg The terrain mask, texelsPerSide^2 texels of R and G (terrain::ocean_mask layout).
		/// @param texelsPerSide Texels per side.
		/// @param texelSize World size of one texel (m).
		/// @param outRgba Receives texelsPerSide^2 * 4 bytes.
		/// @remark A shore texel is one where the ocean is not drawn (R = 0) or the ground reaches the
		///         sea level (G = 0). Distances use a two-pass 3-4 chamfer transform, within a few
		///         percent of Euclidean - plenty for foam bands.
		inline void BuildShaderMask(const std::vector<uint8>& rg, const uint32 texelsPerSide, const float texelSize, std::vector<uint8>& outRgba)
		{
			const size_t count = static_cast<size_t>(texelsPerSide) * texelsPerSide;
			outRgba.assign(count * 4, 255);
			if (rg.size() < count * 2 || texelsPerSide == 0)
			{
				return;
			}

			constexpr float straight = 1.0f;
			constexpr float diagonal = 1.41421356f;
			const float infinity = std::numeric_limits<float>::max() * 0.5f;

			std::vector<float> distance(count);
			for (size_t i = 0; i < count; ++i)
			{
				const bool shore = rg[i * 2] == 0 || rg[i * 2 + 1] == 0;
				distance[i] = shore ? 0.0f : infinity;
			}

			const int32 side = static_cast<int32>(texelsPerSide);
			const auto at = [&](const int32 x, const int32 z) -> float&
			{
				return distance[static_cast<size_t>(z) * texelsPerSide + static_cast<size_t>(x)];
			};
			const auto relax = [&](float& target, const int32 x, const int32 z, const float cost)
			{
				if (x >= 0 && z >= 0 && x < side && z < side)
				{
					target = std::min(target, at(x, z) + cost);
				}
			};

			for (int32 z = 0; z < side; ++z)
			{
				for (int32 x = 0; x < side; ++x)
				{
					float& d = at(x, z);
					relax(d, x - 1, z, straight);
					relax(d, x, z - 1, straight);
					relax(d, x - 1, z - 1, diagonal);
					relax(d, x + 1, z - 1, diagonal);
				}
			}

			for (int32 z = side - 1; z >= 0; --z)
			{
				for (int32 x = side - 1; x >= 0; --x)
				{
					float& d = at(x, z);
					relax(d, x + 1, z, straight);
					relax(d, x, z + 1, straight);
					relax(d, x + 1, z + 1, diagonal);
					relax(d, x - 1, z + 1, diagonal);
				}
			}

			for (size_t i = 0; i < count; ++i)
			{
				const float metres = distance[i] * texelSize;
				outRgba[i * 4] = rg[i * 2];
				outRgba[i * 4 + 1] = rg[i * 2 + 1];
				outRgba[i * 4 + 2] = static_cast<uint8>(std::lround(std::clamp(metres / DistanceRange, 0.0f, 1.0f) * 255.0f));
				outRgba[i * 4 + 3] = 255;
			}
		}
	}
}
