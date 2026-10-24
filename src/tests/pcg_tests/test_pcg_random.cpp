// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/pcg_random.h"

#include <cmath>

using namespace mmo;

TEST_CASE("Hash is deterministic and sensitive to every input", "[pcg_random]")
{
	CHECK(pcg::Hash(1, 2, 3, 4) == pcg::Hash(1, 2, 3, 4));
	CHECK(pcg::Hash(1, 2, 3, 4) != pcg::Hash(2, 2, 3, 4));
	CHECK(pcg::Hash(1, 2, 3, 4) != pcg::Hash(1, 2, 3, 5));
	CHECK(pcg::Hash(1, 2, 3, 4) != pcg::Hash(1, 3, 3, 4));
	CHECK(pcg::Hash(1, 2, 3, 4) != pcg::Hash(1, 2, 4, 4));
}

TEST_CASE("Hash regression lock", "[pcg_random]")
{
	CHECK(pcg::Hash(0, 0, 0, 0) == 0x2130748aaac80268ULL);
}

TEST_CASE("HashToUnit stays in range with a uniform mean", "[pcg_random]")
{
	double sum = 0.0;
	for (int64 i = 0; i < 100000; ++i)
	{
		const float u = pcg::HashToUnit(pcg::Hash(7, i));
		REQUIRE(u >= 0.0f);
		REQUIRE(u < 1.0f);
		sum += u;
	}
	CHECK(std::abs(sum / 100000.0 - 0.5) < 0.01);
}

TEST_CASE("HashRange maps into the requested interval", "[pcg_random]")
{
	for (int64 i = 0; i < 1000; ++i)
	{
		const float v = pcg::HashRange(pcg::Hash(3, i), -2.0f, 5.0f);
		REQUIRE(v >= -2.0f);
		REQUIRE(v < 5.0f);
	}
}

TEST_CASE("ValueNoise is deterministic, bounded, seeded and continuous", "[pcg_random]")
{
	CHECK(pcg::ValueNoise(1, 10.3f, 7.7f, 0.1f) == pcg::ValueNoise(1, 10.3f, 7.7f, 0.1f));
	CHECK(pcg::ValueNoise(1, 10.3f, 7.7f, 0.1f) != pcg::ValueNoise(2, 10.3f, 7.7f, 0.1f));

	for (int i = 0; i < 2000; ++i)
	{
		const float x = i * 0.37f - 300.0f;
		const float z = i * 0.91f - 500.0f;
		const float n = pcg::ValueNoise(5, x, z, 0.2f);
		REQUIRE(n >= -1.0f);
		REQUIRE(n <= 1.0f);
		REQUIRE(std::abs(n - pcg::ValueNoise(5, x + 0.001f, z, 0.2f)) < 0.01f);
	}
}

TEST_CASE("FractalNoise is bounded and deterministic", "[pcg_random]")
{
	for (int i = 0; i < 500; ++i)
	{
		const float n = pcg::FractalNoise(9, i * 1.3f, i * 0.7f, 0.05f, 4, 0.5f);
		REQUIRE(n >= -1.0f);
		REQUIRE(n <= 1.0f);
		REQUIRE(n == pcg::FractalNoise(9, i * 1.3f, i * 0.7f, 0.05f, 4, 0.5f));
	}
}
