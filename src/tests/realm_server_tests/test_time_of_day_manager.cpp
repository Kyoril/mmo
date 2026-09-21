// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "realm_server/time_of_day_manager.h"
#include "base/clock.h"
#include "game/time_of_day.h"

using namespace mmo;

namespace
{
	struct FakeSystemClock
	{
		GameTime timeOfDay = 0;

		TimeOfDayManager::SystemTimeOfDayProvider Provider()
		{
			return [this]() { return timeOfDay; };
		}
	};
}

TEST_CASE("TimeOfDayManager follows the system time of day by default", "[time_of_day]")
{
	FakeSystemClock clock;
	clock.timeOfDay = 9 * constants::OneHour;
	TimeOfDayManager manager(clock.Provider());

	CHECK_FALSE(manager.IsOverridden());
	CHECK(manager.GetOffset() == 0);
	CHECK(manager.GetTimeOfDay() == 9 * constants::OneHour);

	clock.timeOfDay += constants::OneMinute;
	CHECK(manager.GetTimeOfDay() == 9 * constants::OneHour + constants::OneMinute);
}

TEST_CASE("TimeOfDayManager keeps an override running as an offset to the system clock", "[time_of_day]")
{
	FakeSystemClock clock;
	clock.timeOfDay = 20 * constants::OneHour;
	TimeOfDayManager manager(clock.Provider());

	GameTime notifiedTime = 0;
	uint32 notifiedTransition = 0;
	int notifications = 0;
	manager.timeOfDayChanged.connect([&](const GameTime timeOfDay, const uint32 transitionMs)
	{
		notifiedTime = timeOfDay;
		notifiedTransition = transitionMs;
		++notifications;
	});

	manager.SetTimeOfDay(6 * constants::OneHour, 8000);

	CHECK(manager.IsOverridden());
	CHECK(notifications == 1);
	CHECK(notifiedTime == 6 * constants::OneHour);
	CHECK(notifiedTransition == 8000);
	CHECK(manager.GetTimeOfDay() == 6 * constants::OneHour);
	CHECK(manager.GetOffset() == 10 * constants::OneHour);

	// The game clock keeps ticking with the system clock, across the system's midnight
	clock.timeOfDay = 5 * constants::OneHour;
	CHECK(manager.GetTimeOfDay() == 15 * constants::OneHour);
}

TEST_CASE("TimeOfDayManager reset returns to the system time of day", "[time_of_day]")
{
	FakeSystemClock clock;
	clock.timeOfDay = 13 * constants::OneHour;
	TimeOfDayManager manager(clock.Provider());

	manager.SetTimeOfDay(1 * constants::OneHour, 0);

	GameTime notifiedTime = 0;
	manager.timeOfDayChanged.connect([&](const GameTime timeOfDay, uint32)
	{
		notifiedTime = timeOfDay;
	});

	manager.Reset(5000);

	CHECK_FALSE(manager.IsOverridden());
	CHECK(manager.GetOffset() == 0);
	CHECK(manager.GetTimeOfDay() == 13 * constants::OneHour);
	CHECK(notifiedTime == 13 * constants::OneHour);
}

TEST_CASE("TimeOfDayManager clamps the transition length", "[time_of_day]")
{
	FakeSystemClock clock;
	TimeOfDayManager manager(clock.Provider());

	uint32 notifiedTransition = 0;
	manager.timeOfDayChanged.connect([&](GameTime, const uint32 transitionMs)
	{
		notifiedTransition = transitionMs;
	});

	manager.SetTimeOfDay(constants::OneHour, 10 * 60 * 1000);
	CHECK(notifiedTransition == MaxTimeOfDayTransitionMs);
}
