// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include <cmath>

#include "fake_water_source.h"
#include "game_client/water_ambience_scan.h"

using namespace mmo;

namespace
{
	constexpr uint32 riverType = 1;
	constexpr uint32 oceanType = 2;

	const Vector3 ear(0.0f, 1.6f, 0.0f);

	WaterAmbienceProfile RiverProfile()
	{
		WaterAmbienceProfile profile;
		profile.waterType = riverType;
		profile.shoreOnly = false;
		profile.range = 40.0f;
		return profile;
	}

	WaterAmbienceProfile ShoreProfile()
	{
		WaterAmbienceProfile profile;
		profile.waterType = oceanType;
		profile.shoreOnly = true;
		profile.range = 40.0f;
		return profile;
	}

	/// River water of type 1 at surface height 0 wherever minX <= x <= maxX.
	FakeWaterSource RiverBand(const float minX, const float maxX, const float surfaceY = 0.0f)
	{
		return FakeWaterSource([=](const float x, float)
		{
			if (x >= minX && x <= maxX)
			{
				return MakeWater(riverType, surfaceY);
			}

			return WaterSample();
		});
	}
}

TEST_CASE("no water gives level 0", "[water_ambience_scan]")
{
	const FakeWaterSource source([](float, float) { return WaterSample(); });

	const auto fields = ScanWaterAmbience(source, ear, { RiverProfile() });

	REQUIRE(fields.size() == 1);
	CHECK(fields[0].waterType == riverType);
	CHECK(fields[0].level == 0.0f);
}

TEST_CASE("river band on the left points left", "[water_ambience_scan]")
{
	const FakeWaterSource source = RiverBand(-17.0f, -5.0f);

	const auto fields = ScanWaterAmbience(source, ear, { RiverProfile() });

	REQUIRE(fields.size() == 1);
	// The band runs the full scan length like a real straight river. Its water spans roughly
	// +-80 degrees around the ear, so the mean direction is only about 0.7 to one side.
	CHECK(fields[0].level == Approx(1.0f));
	CHECK(fields[0].direction.x < -0.6f);
	CHECK(fields[0].GetFocus() > 0.6f);
}

TEST_CASE("level falls with distance", "[water_ambience_scan]")
{
	const FakeWaterSource source = RiverBand(-42.0f, -30.0f);

	const auto fields = ScanWaterAmbience(source, ear, { RiverProfile() });

	REQUIRE(fields.size() == 1);
	CHECK(fields[0].level < 0.15f);
}

TEST_CASE("surrounded gives low focus", "[water_ambience_scan]")
{
	const FakeWaterSource source([](float, float) { return MakeWater(riverType, ear.y - 0.5f); });

	const auto fields = ScanWaterAmbience(source, ear, { RiverProfile() });

	REQUIRE(fields.size() == 1);
	CHECK(fields[0].level > 0.0f);
	CHECK(fields[0].GetFocus() < 0.4f);
}

TEST_CASE("water below a bridge points down", "[water_ambience_scan]")
{
	const FakeWaterSource source = RiverBand(-6.0f, 6.0f, -4.0f);

	const auto fields = ScanWaterAmbience(source, ear, { RiverProfile() });

	REQUIRE(fields.size() == 1);
	CHECK(fields[0].direction.y < -0.3f);
	CHECK(std::abs(fields[0].direction.x) < 0.2f);
}

TEST_CASE("shore only ignores open water", "[water_ambience_scan]")
{
	SECTION("only water next to land counts")
	{
		const FakeWaterSource source([](const float x, float)
		{
			if (x < 0.0f)
			{
				return MakeWater(oceanType, 0.0f);
			}

			return WaterSample();
		});

		const auto fields = ScanWaterAmbience(source, ear, { ShoreProfile() });

		REQUIRE(fields.size() == 1);
		CHECK(fields[0].direction.x > -0.95f);
		CHECK(fields[0].level > 0.0f);

		// All contributing points sit at x > -GridSpacing * 1.5: an ocean only one column wide
		// gives exactly the same result as the open ocean behind it.
		const FakeWaterSource firstColumnOnly([](const float x, float)
		{
			if (x < 0.0f && x > -water_ambience::GridSpacing * 1.5f)
			{
				return MakeWater(oceanType, 0.0f);
			}

			return WaterSample();
		});

		const auto columnFields = ScanWaterAmbience(firstColumnOnly, ear, { ShoreProfile() });

		REQUIRE(columnFields.size() == 1);
		CHECK(columnFields[0].level == Approx(fields[0].level));
		CHECK(columnFields[0].direction.x == Approx(fields[0].direction.x));
	}

	SECTION("open ocean without land is silent")
	{
		const FakeWaterSource source([](float, float) { return MakeWater(oceanType, 0.0f); });

		const auto fields = ScanWaterAmbience(source, ear, { ShoreProfile() });

		REQUIRE(fields.size() == 1);
		CHECK(fields[0].level == 0.0f);
	}
}

TEST_CASE("beach under shallow water counts as shore", "[water_ambience_scan]")
{
	const FakeWaterSource source([](const float x, float)
	{
		WaterSample sample = MakeWater(oceanType, 0.0f);
		if (x >= 0.0f)
		{
			sample.hasGround = true;
			sample.groundY = 0.5f;
		}

		return sample;
	});

	const auto fields = ScanWaterAmbience(source, ear, { ShoreProfile() });

	REQUIRE(fields.size() == 1);
	CHECK(fields[0].level > 0.0f);
}

TEST_CASE("speed is the weighted mean", "[water_ambience_scan]")
{
	SECTION("mixed speeds land between the extremes")
	{
		const FakeWaterSource source([](const float x, float)
		{
			if (x < -10.0f && x >= -17.0f)
			{
				return MakeWater(riverType, 0.0f, 1.0f);
			}

			if (x >= -10.0f && x < -5.0f)
			{
				return MakeWater(riverType, 0.0f, 0.0f);
			}

			return WaterSample();
		});

		const auto fields = ScanWaterAmbience(source, ear, { RiverProfile() });

		REQUIRE(fields.size() == 1);
		CHECK(fields[0].speed > 0.0f);
		CHECK(fields[0].speed < 1.0f);
	}

	SECTION("uniform speed is kept")
	{
		const FakeWaterSource source([](const float x, float)
		{
			if (x >= -17.0f && x <= -5.0f)
			{
				return MakeWater(riverType, 0.0f, 0.6f);
			}

			return WaterSample();
		});

		const auto fields = ScanWaterAmbience(source, ear, { RiverProfile() });

		REQUIRE(fields.size() == 1);
		CHECK(fields[0].speed == Approx(0.6f));
	}
}

TEST_CASE("river and shore aggregate separately", "[water_ambience_scan]")
{
	const FakeWaterSource source([](const float x, float)
	{
		if (x >= -17.0f && x <= -5.0f)
		{
			return MakeWater(riverType, 0.0f);
		}

		if (x > 5.0f)
		{
			return MakeWater(oceanType, 0.0f);
		}

		return WaterSample();
	});

	const auto fields = ScanWaterAmbience(source, ear, { RiverProfile(), ShoreProfile() });

	REQUIRE(fields.size() == 2);
	CHECK(fields[0].waterType == riverType);
	CHECK(fields[1].waterType == oceanType);
	CHECK(fields[0].direction.x < 0.0f);
	CHECK(fields[1].direction.x > 0.0f);
}

TEST_CASE("scan points are world-snapped", "[water_ambience_scan]")
{
	// A band edge at x = -20 sits between scan columns. With world-snapped points, moving the
	// ear inside one grid cell keeps the sampled set and only shifts distances slightly.
	const FakeWaterSource source = RiverBand(-60.0f, -20.0f);

	const float cellStart = 0.0f;
	const Vector3 earA(cellStart + 1.0f, ear.y, 1.0f);
	const Vector3 earB(cellStart + 2.0f, ear.y, 1.0f);

	const auto fieldsA = ScanWaterAmbience(source, earA, { RiverProfile() });
	const auto fieldsB = ScanWaterAmbience(source, earB, { RiverProfile() });

	REQUIRE(fieldsA.size() == 1);
	REQUIRE(fieldsB.size() == 1);
	CHECK(fieldsA[0].level > 0.0f);
	CHECK(std::abs(fieldsA[0].level - fieldsB[0].level) < 0.05f);
}

TEST_CASE("unloaded page reads as dry", "[water_ambience_scan]")
{
	const FakeWaterSource source([](const float x, float)
	{
		if (x < 0.0f)
		{
			// Page not loaded: the source reports no water.
			return WaterSample();
		}

		return MakeWater(riverType, 0.0f);
	});

	const auto fields = ScanWaterAmbience(source, ear, { RiverProfile() });

	REQUIRE(fields.size() == 1);
	CHECK(fields[0].direction.x > 0.0f);
}
