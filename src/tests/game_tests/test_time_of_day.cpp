// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game/game_time_component.h"
#include "game/time_of_day.h"

using namespace mmo;

TEST_CASE("ParseTimeOfDay accepts hours, minutes and seconds", "[time_of_day]")
{
	GameTime value = 0;

	REQUIRE(ParseTimeOfDay("0", value));
	CHECK(value == 0);

	REQUIRE(ParseTimeOfDay("21", value));
	CHECK(value == 21 * constants::OneHour);

	REQUIRE(ParseTimeOfDay("6:30", value));
	CHECK(value == 6 * constants::OneHour + 30 * constants::OneMinute);

	REQUIRE(ParseTimeOfDay("23:59:59", value));
	CHECK(value == 23 * constants::OneHour + 59 * constants::OneMinute + 59 * constants::OneSecond);
}

TEST_CASE("ParseTimeOfDay rejects malformed and out of range input", "[time_of_day]")
{
	GameTime value = 12345;

	CHECK_FALSE(ParseTimeOfDay("", value));
	CHECK_FALSE(ParseTimeOfDay("24", value));
	CHECK_FALSE(ParseTimeOfDay("12:60", value));
	CHECK_FALSE(ParseTimeOfDay("12:00:60", value));
	CHECK_FALSE(ParseTimeOfDay("12:", value));
	CHECK_FALSE(ParseTimeOfDay(":30", value));
	CHECK_FALSE(ParseTimeOfDay("1:2:3:4", value));
	CHECK_FALSE(ParseTimeOfDay("123", value));
	CHECK_FALSE(ParseTimeOfDay("noon", value));
	CHECK_FALSE(ParseTimeOfDay("-1", value));

	// A failed parse leaves the output untouched
	CHECK(value == 12345);
}

TEST_CASE("FormatTimeOfDay pads and wraps", "[time_of_day]")
{
	CHECK(FormatTimeOfDay(0) == "00:00:00");
	CHECK(FormatTimeOfDay(7 * constants::OneHour + 5 * constants::OneMinute + 9 * constants::OneSecond) == "07:05:09");
	CHECK(FormatTimeOfDay(constants::OneDay + constants::OneHour) == "01:00:00");
}

TEST_CASE("GetShortestTimeOfDayDelta takes the short way around midnight", "[time_of_day]")
{
	const int64 hour = static_cast<int64>(constants::OneHour);

	CHECK(GetShortestTimeOfDayDelta(10 * constants::OneHour, 12 * constants::OneHour) == 2 * hour);
	CHECK(GetShortestTimeOfDayDelta(12 * constants::OneHour, 10 * constants::OneHour) == -2 * hour);
	CHECK(GetShortestTimeOfDayDelta(23 * constants::OneHour, 1 * constants::OneHour) == 2 * hour);
	CHECK(GetShortestTimeOfDayDelta(1 * constants::OneHour, 23 * constants::OneHour) == -2 * hour);

	// Exactly half a day resolves forwards
	CHECK(GetShortestTimeOfDayDelta(0, 12 * constants::OneHour) == 12 * hour);
	CHECK(GetShortestTimeOfDayDelta(12 * constants::OneHour, 0) == 12 * hour);
}

TEST_CASE("GameTimeComponent transition blends towards the target and lands on it", "[time_of_day]")
{
	GameTimeComponent gameTime(10 * constants::OneHour, 1.0f);
	gameTime.Update(1000);	// First update only primes the real-time reference

	gameTime.TransitionTo(12 * constants::OneHour, 8000);

	CHECK(gameTime.IsTransitioning());
	CHECK(gameTime.GetTargetTime() == 12 * constants::OneHour);
	CHECK(gameTime.GetTime() == 10 * constants::OneHour);

	// Halfway through the blend the eased value sits exactly between start and target (plus
	// the four seconds that passed on the clock itself)
	gameTime.Update(5000);
	CHECK(gameTime.IsTransitioning());
	CHECK(gameTime.GetTime() == 11 * constants::OneHour + 4000);
	CHECK(gameTime.GetTargetTime() == 12 * constants::OneHour + 4000);

	gameTime.Update(9000);
	CHECK_FALSE(gameTime.IsTransitioning());
	CHECK(gameTime.GetTime() == 12 * constants::OneHour + 8000);
}

TEST_CASE("GameTimeComponent transition moves backwards across midnight when that is shorter", "[time_of_day]")
{
	GameTimeComponent gameTime(1 * constants::OneHour, 1.0f);
	gameTime.Update(1000);

	gameTime.TransitionTo(23 * constants::OneHour, 1000);

	// Without time passing, the reported time is still the start time
	CHECK(gameTime.GetTime() == 1 * constants::OneHour);

	// The blend passes through midnight rather than through noon
	gameTime.Update(1500);
	const GameTime halfway = gameTime.GetTime();
	CHECK((halfway < 1 * constants::OneHour || halfway > 23 * constants::OneHour));
	CHECK(halfway == 500);
}

TEST_CASE("GameTimeComponent SyncTime keeps a transition running, SetTime cancels it", "[time_of_day]")
{
	GameTimeComponent gameTime(6 * constants::OneHour, 1.0f);
	gameTime.Update(1000);
	gameTime.TransitionTo(8 * constants::OneHour, 10000);

	gameTime.SyncTime(8 * constants::OneHour);
	CHECK(gameTime.IsTransitioning());
	CHECK(gameTime.GetTime() == 6 * constants::OneHour);

	gameTime.SetTime(8 * constants::OneHour);
	CHECK_FALSE(gameTime.IsTransitioning());
	CHECK(gameTime.GetTime() == 8 * constants::OneHour);
}

TEST_CASE("GameTimeComponent zero-length transition sets the time immediately", "[time_of_day]")
{
	GameTimeComponent gameTime(6 * constants::OneHour, 1.0f);
	gameTime.TransitionTo(18 * constants::OneHour, 0);

	CHECK_FALSE(gameTime.IsTransitioning());
	CHECK(gameTime.GetTime() == 18 * constants::OneHour);
}
