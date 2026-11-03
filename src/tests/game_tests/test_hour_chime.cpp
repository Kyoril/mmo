// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game/hour_chime.h"

using namespace mmo;

namespace
{
	GameTime At(const uint32 hour, const uint32 minute = 0, const uint32 second = 0)
	{
		return hour * constants::OneHour + minute * constants::OneMinute + second * constants::OneSecond;
	}
}

TEST_CASE("GetClockStrikeCount counts on a 12-hour clock", "[hour_chime]")
{
	CHECK(GetClockStrikeCount(At(0)) == 12);
	CHECK(GetClockStrikeCount(At(1)) == 1);
	CHECK(GetClockStrikeCount(At(2, 30)) == 2);
	CHECK(GetClockStrikeCount(At(11, 59, 59)) == 11);
	CHECK(GetClockStrikeCount(At(12)) == 12);
	CHECK(GetClockStrikeCount(At(13)) == 1);
	CHECK(GetClockStrikeCount(At(14)) == 2);
	CHECK(GetClockStrikeCount(At(23)) == 11);
	CHECK(GetClockStrikeCount(At(1) + constants::OneDay) == 1);
}

TEST_CASE("GetHourOfDayEventValue reports midnight as 24", "[hour_chime]")
{
	CHECK(GetHourOfDayEventValue(At(0)) == 24);
	CHECK(GetHourOfDayEventValue(At(1)) == 1);
	CHECK(GetHourOfDayEventValue(At(13, 20)) == 13);
	CHECK(GetHourOfDayEventValue(At(23, 59)) == 23);
}

TEST_CASE("GetHourOfDay reports the hour on a 24-hour clock", "[hour_chime]")
{
	CHECK(GetHourOfDay(At(0)) == 0);
	CHECK(GetHourOfDay(At(0, 59, 59)) == 0);
	CHECK(GetHourOfDay(At(6)) == 6);
	CHECK(GetHourOfDay(At(21, 30)) == 21);
	CHECK(GetHourOfDay(At(23, 59, 59)) == 23);
	CHECK(GetHourOfDay(At(5) + constants::OneDay) == 5);
}

TEST_CASE("HourChime announces a full hour once when the clock runs into it", "[hour_chime]")
{
	HourChime chime;
	uint32 hour = 99;

	CHECK_FALSE(chime.Update(At(13, 59, 58), hour));
	CHECK_FALSE(chime.Update(At(13, 59, 59), hour));

	REQUIRE(chime.Update(At(14), hour));
	CHECK(hour == 14);

	// Later ticks inside the window do not announce it again
	CHECK_FALSE(chime.Update(At(14, 0, 1), hour));
	CHECK_FALSE(chime.Update(At(14, 0, 59), hour));
	CHECK_FALSE(chime.Update(At(14, 1), hour));
	CHECK_FALSE(chime.Update(At(14, 59, 59), hour));

	REQUIRE(chime.Update(At(15, 0, 0) + 40, hour));
	CHECK(hour == 15);
}

TEST_CASE("HourChime announces midnight and wraps past one day", "[hour_chime]")
{
	HourChime chime;
	uint32 hour = 99;

	CHECK_FALSE(chime.Update(At(23, 59, 59), hour));
	REQUIRE(chime.Update(constants::OneDay + 5, hour));
	CHECK(hour == 0);
}

TEST_CASE("HourChime announces a time of day that was set to a full hour", "[hour_chime]")
{
	HourChime chime;
	uint32 hour = 99;

	CHECK_FALSE(chime.Update(At(9, 12), hour));

	// settime 14 jumps straight into the window
	REQUIRE(chime.Update(At(14), hour));
	CHECK(hour == 14);

	// settime 14:30 is not a full hour
	CHECK_FALSE(chime.Update(At(14, 30), hour));

	// ...and setting 14 again afterwards rings again, which is what a tester expects
	REQUIRE(chime.Update(At(14), hour));
	CHECK(hour == 14);
}

TEST_CASE("HourChime does not ring twice when the same hour is set again inside the window", "[hour_chime]")
{
	HourChime chime;
	uint32 hour = 99;

	REQUIRE(chime.Update(At(14, 0, 10), hour));
	CHECK_FALSE(chime.Update(At(14), hour));
}

TEST_CASE("HourChime announces a different full hour set inside the window", "[hour_chime]")
{
	HourChime chime;
	uint32 hour = 99;

	REQUIRE(chime.Update(At(14), hour));
	REQUIRE(chime.Update(At(18), hour));
	CHECK(hour == 18);
}
