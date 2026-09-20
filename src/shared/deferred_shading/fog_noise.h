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
}
