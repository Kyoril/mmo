// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "grid_layout.h"

namespace mmo::pcg
{
	/// SplitMix64 finaliser over the combined inputs. Pure, platform independent.
	uint64 Hash(uint64 seed, int64 a, int64 b = 0, uint64 salt = 0);

	/// [0,1) from the top 24 bits of a hash.
	float HashToUnit(uint64 hash);

	/// [lo,hi) helper: lo + HashToUnit(h) * (hi - lo).
	float HashRange(uint64 hash, float lo, float hi);

	/// Seeded 2D value noise (smoothstep-interpolated lattice of Hash values), output [-1,1].
	float ValueNoise(uint64 seed, float x, float z, float frequency);

	/// Sum of octaves of ValueNoise, normalised to [-1,1].
	float FractalNoise(uint64 seed, float x, float z, float frequency, uint32 octaves, float persistence);
}
