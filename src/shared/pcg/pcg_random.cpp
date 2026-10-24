// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "pcg_random.h"

#include <cmath>

namespace mmo::pcg
{
	namespace
	{
		uint64 Mix(uint64 z)
		{
			z += 0x9E3779B97F4A7C15ULL;
			z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
			z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
			return z ^ (z >> 31);
		}
	}

	uint64 Hash(uint64 seed, int64 a, int64 b, uint64 salt)
	{
		uint64 h = Mix(seed);
		h = Mix(h ^ static_cast<uint64>(a));
		h = Mix(h ^ static_cast<uint64>(b));
		h = Mix(h ^ salt);
		return h;
	}

	float HashToUnit(uint64 hash)
	{
		return static_cast<float>(hash >> 40) * (1.0f / 16777216.0f);
	}

	float HashRange(uint64 hash, float lo, float hi)
	{
		return lo + HashToUnit(hash) * (hi - lo);
	}

	float ValueNoise(uint64 seed, float x, float z, float frequency)
	{
		const double fx = static_cast<double>(x) * frequency;
		const double fz = static_cast<double>(z) * frequency;
		const double ix = std::floor(fx);
		const double iz = std::floor(fz);
		const float tx = static_cast<float>(fx - ix);
		const float tz = static_cast<float>(fz - iz);
		const float sx = tx * tx * (3.0f - 2.0f * tx);
		const float sz = tz * tz * (3.0f - 2.0f * tz);

		const int64 x0 = static_cast<int64>(ix);
		const int64 z0 = static_cast<int64>(iz);
		auto lattice = [seed](int64 lx, int64 lz)
		{
			return HashToUnit(Hash(seed, lx, lz)) * 2.0f - 1.0f;
		};

		const float v00 = lattice(x0, z0);
		const float v10 = lattice(x0 + 1, z0);
		const float v01 = lattice(x0, z0 + 1);
		const float v11 = lattice(x0 + 1, z0 + 1);
		const float a = v00 + (v10 - v00) * sx;
		const float b = v01 + (v11 - v01) * sx;
		return a + (b - a) * sz;
	}

	float FractalNoise(uint64 seed, float x, float z, float frequency, uint32 octaves, float persistence)
	{
		float sum = 0.0f;
		float amplitude = 1.0f;
		float total = 0.0f;
		float freq = frequency;
		for (uint32 i = 0; i < octaves; ++i)
		{
			sum += amplitude * ValueNoise(seed + i * 0x632BE59BD9B4E019ULL, x, z, freq);
			total += amplitude;
			amplitude *= persistence;
			freq *= 2.0f;
		}
		return total > 0.0f ? sum / total : 0.0f;
	}
}
