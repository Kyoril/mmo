// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/atmosphere_math.h"

#include <cmath>

using namespace mmo::atmosphere;

namespace
{
	/// Midpoint-rule optical depth in double precision, used as ground truth.
	double NumericOpticalDepth(double originY, double dirY, double length, double density, double falloff, double baseHeight)
	{
		constexpr int steps = 20000;
		const double dt = length / steps;
		double sum = 0.0;
		for (int i = 0; i < steps; ++i)
		{
			const double y = originY + dirY * (i + 0.5) * dt;
			sum += density * std::exp(-falloff * (y - baseHeight)) * dt;
		}
		return sum;
	}
}

TEST_CASE("Inverse tonemap round-trips every invertible display value", "[atmosphere]")
{
	for (int i = 0; i <= 999; ++i)
	{
		const float display = static_cast<float>(i) / 1000.0f;
		const float linear = InverseTonemap(display);
		REQUIRE(linear >= 0.0f);
		REQUIRE(Tonemap(linear) == Approx(display).margin(5e-4));
	}
}

TEST_CASE("Inverse tonemap maps black to black and is monotonic", "[atmosphere]")
{
	REQUIRE(InverseTonemap(0.0f) == Approx(0.0f).margin(1e-6));

	float previous = -1.0f;
	for (int i = 0; i <= 999; ++i)
	{
		const float linear = InverseTonemap(static_cast<float>(i) / 1000.0f);
		REQUIRE(linear > previous);
		previous = linear;
	}
}

TEST_CASE("Inverse tonemap clamps display values above the invertible range", "[atmosphere]")
{
	REQUIRE(InverseTonemap(1.5f) == Approx(InverseTonemap(MaxInvertibleDisplay)));
	REQUIRE(std::isfinite(InverseTonemap(1.0f)));
}

TEST_CASE("Closed-form height fog optical depth matches numeric integration", "[atmosphere]")
{
	const float originYs[] = { 0.0f, 12.0f, 40.0f };
	const float dirYs[] = { -0.3f, -0.05f, 0.0f, 0.2f, 0.9f };
	const float lengths[] = { 1.0f, 60.0f, 300.0f };
	const float falloffs[] = { 0.0f, 0.05f, 0.15f };
	constexpr float density = 0.02f;
	constexpr float baseHeight = 0.0f;

	for (const float originY : originYs)
	{
		for (const float dirY : dirYs)
		{
			for (const float length : lengths)
			{
				for (const float falloff : falloffs)
				{
					// The closed form is exact only where the density exponent clamp is inactive.
					const float lowestY = std::min(originY, originY + dirY * length);
					if (-falloff * (lowestY - baseHeight) > MaxDensityExponent)
					{
						continue;
					}

					const double expected = NumericOpticalDepth(originY, dirY, length, density, falloff, baseHeight);
					const float actual = FogOpticalDepth(originY, dirY, length, density, falloff, baseHeight);

					INFO("originY=" << originY << " dirY=" << dirY << " length=" << length << " falloff=" << falloff);
					REQUIRE(actual == Approx(expected).epsilon(0.01).margin(1e-6));
				}
			}
		}
	}
}

TEST_CASE("Fog density clamps its exponent far below the base height", "[atmosphere]")
{
	const float deep = FogDensityAt(-10000.0f, 0.02f, 0.05f, 0.0f);
	REQUIRE(std::isfinite(deep));
	REQUIRE(deep == Approx(0.02f * std::exp(MaxDensityExponent)));
}

TEST_CASE("Scatter phase is isotropic at zero anisotropy and forward-peaked otherwise", "[atmosphere]")
{
	REQUIRE(ScatterPhase(0.0f, 1.0f) == Approx(1.0f));
	REQUIRE(ScatterPhase(0.0f, -1.0f) == Approx(1.0f));

	const float towardSun = ScatterPhase(0.7f, 1.0f);
	const float awayFromSun = ScatterPhase(0.7f, -1.0f);
	REQUIRE(towardSun > 10.0f);
	REQUIRE(awayFromSun < 1.0f);
	REQUIRE(awayFromSun > 0.0f);
}
