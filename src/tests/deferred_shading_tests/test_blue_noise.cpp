// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/fog_noise.h"

#include <array>
#include <cstdint>

using namespace mmo;

namespace
{
	/// Variance of the 3x3 toroidal box-filtered texture: what is left of a pattern after a low-pass
	/// filter. Blue noise has almost no low-frequency energy, so it keeps far less than white noise.
	double lowPassVariance(const std::vector<uint8>& texels, const uint32 size)
	{
		double sum = 0.0;
		double sumSquares = 0.0;
		for (uint32 y = 0; y < size; ++y)
		{
			for (uint32 x = 0; x < size; ++x)
			{
				double box = 0.0;
				for (int32 dy = -1; dy <= 1; ++dy)
				{
					for (int32 dx = -1; dx <= 1; ++dx)
					{
						const uint32 sx = (x + size + dx) % size;
						const uint32 sy = (y + size + dy) % size;
						box += texels[sy * size + sx];
					}
				}

				box /= 9.0;
				sum += box;
				sumSquares += box * box;
			}
		}

		const double count = static_cast<double>(size) * size;
		const double mean = sum / count;
		return sumSquares / count - mean * mean;
	}
}

TEST_CASE("Blue noise has the requested size and is deterministic per seed", "[volumetric_fog]")
{
	const std::vector<uint8> a = GenerateBlueNoise(32, 7);
	const std::vector<uint8> b = GenerateBlueNoise(32, 7);
	const std::vector<uint8> c = GenerateBlueNoise(32, 8);

	REQUIRE(a.size() == 32u * 32u);
	CHECK(a == b);
	CHECK(a != c);
}

TEST_CASE("Blue noise uses every value level equally often", "[volumetric_fog]")
{
	// A rank map: 4096 texels over 256 levels must put exactly 16 texels on each level, so a per-froxel
	// jitter read from it is uniformly distributed.
	const std::vector<uint8> noise = GenerateBlueNoise(64, 1);

	std::array<int, 256> histogram{};
	for (const uint8 value : noise)
	{
		++histogram[value];
	}

	for (const int count : histogram)
	{
		CHECK(count == 16);
	}
}

TEST_CASE("Blue noise keeps far less low-frequency energy than white noise", "[volumetric_fog]")
{
	constexpr uint32 size = 64;
	const std::vector<uint8> blue = GenerateBlueNoise(size, 1);

	// The same values in a scrambled order: white noise with the identical histogram.
	std::vector<uint8> white = blue;
	uint32 state = 12345u;
	for (size_t i = white.size() - 1; i > 0; --i)
	{
		state = state * 1664525u + 1013904223u;
		std::swap(white[i], white[(state >> 8) % (i + 1)]);
	}

	// White noise keeps 1/9 of its variance through a 3x3 box; good blue noise keeps a small fraction of that.
	CHECK(lowPassVariance(blue, size) < lowPassVariance(white, size) * 0.25);
}
