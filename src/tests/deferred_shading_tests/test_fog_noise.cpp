// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/fog_noise.h"

#include <cstdlib>

using namespace mmo;

namespace
{
	int voxel(const std::vector<uint8>& noise, const uint32 size, const uint32 x, const uint32 y, const uint32 z)
	{
		return noise[static_cast<size_t>(z) * size * size + static_cast<size_t>(y) * size + x];
	}
}

TEST_CASE("Fog noise has the requested size and is deterministic per seed", "[volumetric_fog]")
{
	const std::vector<uint8> a = GenerateFogNoise(32, 7);
	const std::vector<uint8> b = GenerateFogNoise(32, 7);
	const std::vector<uint8> c = GenerateFogNoise(32, 8);

	REQUIRE(a.size() == 32u * 32u * 32u);
	CHECK(a == b);
	CHECK(a != c);
}

TEST_CASE("Fog noise spans the value range with a centred mean", "[volumetric_fog]")
{
	const std::vector<uint8> noise = GenerateFogNoise(64, 1);

	int minimum = 255;
	int maximum = 0;
	double sum = 0.0;
	for (const uint8 value : noise)
	{
		minimum = std::min<int>(minimum, value);
		maximum = std::max<int>(maximum, value);
		sum += value;
	}

	CHECK(minimum == 0);
	CHECK(maximum == 255);

	const double mean = sum / static_cast<double>(noise.size());
	// The centring remap in GenerateFogNoise pivots each sample about the volume's own measured mean,
	// so it lands close to but not exactly on 128 for any given seed (measured ~125.0 for this one).
	CHECK(mean >= 124.0);
	CHECK(mean <= 130.0);
}

TEST_CASE("Fog noise tiles seamlessly on every axis", "[volumetric_fog]")
{
	constexpr uint32 size = 64;
	const std::vector<uint8> noise = GenerateFogNoise(size, 3);

	int interiorX = 0;
	int interiorY = 0;
	int interiorZ = 0;
	int wrapX = 0;
	int wrapY = 0;
	int wrapZ = 0;

	for (uint32 a = 0; a < size; ++a)
	{
		for (uint32 b = 0; b < size; ++b)
		{
			for (uint32 i = 0; i + 1 < size; ++i)
			{
				interiorX = std::max(interiorX, std::abs(voxel(noise, size, i + 1, a, b) - voxel(noise, size, i, a, b)));
				interiorY = std::max(interiorY, std::abs(voxel(noise, size, a, i + 1, b) - voxel(noise, size, a, i, b)));
				interiorZ = std::max(interiorZ, std::abs(voxel(noise, size, a, b, i + 1) - voxel(noise, size, a, b, i)));
			}

			wrapX = std::max(wrapX, std::abs(voxel(noise, size, 0, a, b) - voxel(noise, size, size - 1, a, b)));
			wrapY = std::max(wrapY, std::abs(voxel(noise, size, a, 0, b) - voxel(noise, size, a, size - 1, b)));
			wrapZ = std::max(wrapZ, std::abs(voxel(noise, size, a, b, 0) - voxel(noise, size, a, b, size - 1)));
		}
	}

	CHECK(wrapX <= interiorX * 3 / 2);
	CHECK(wrapY <= interiorY * 3 / 2);
	CHECK(wrapZ <= interiorZ * 3 / 2);
}
