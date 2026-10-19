// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <vector>

namespace mmo
{
	/// @brief Generates a tiling 3D fractal value noise volume for the volumetric fog.
	/// @param size Edge length in texels. The volume repeats seamlessly every size texels on each axis.
	/// @param seed Selects the pattern; the same seed always yields the same volume.
	/// @return size^3 texels, x fastest, then y, then z, stretched to span [0, 255].
	/// @remark Dependency-free so the headless deferred_shading_tests target can compile it.
	[[nodiscard]] std::vector<uint8> GenerateFogNoise(uint32 size, uint32 seed);

	/// @brief Generates a tiling 2D blue noise texture (void-and-cluster rank map) for per-froxel jitter.
	/// @param size Edge length in texels. The texture repeats seamlessly every size texels on both axes.
	/// @param seed Selects the pattern; the same seed always yields the same texture.
	/// @return size^2 texels, x fastest. Every value level is used equally often (as far as size^2 / 256
	///         allows), so a jitter read from the texture is uniformly distributed, and neighbouring texels
	///         differ as much as possible, so a jitter pattern carries almost no low-frequency noise.
	/// @remark Dependency-free so the headless deferred_shading_tests target can compile it. Costs O(size^4):
	///         keep size at 64 or below when generating at startup.
	[[nodiscard]] std::vector<uint8> GenerateBlueNoise(uint32 size, uint32 seed);
}
