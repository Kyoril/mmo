// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game/shutdown_countdown.h"

#include <vector>

using namespace mmo;

namespace
{
	std::vector<uint32> MarksFrom(uint32 remaining)
	{
		std::vector<uint32> marks;
		while ((remaining = NextShutdownAnnouncement(remaining)) != 0)
		{
			marks.push_back(remaining);
		}
		return marks;
	}
}

TEST_CASE("Shutdown announcements follow the fixed schedule below 30 minutes", "[shutdown]")
{
	CHECK(MarksFrom(1800) == std::vector<uint32>{ 900, 600, 300, 240, 180, 120, 60, 45, 30, 15 });
	CHECK(MarksFrom(20) == std::vector<uint32>{ 15 });
	CHECK(MarksFrom(15).empty());
	CHECK(MarksFrom(1).empty());
	CHECK(NextShutdownAnnouncement(0) == 0);
}

TEST_CASE("Shutdown announcements are hourly above one hour", "[shutdown]")
{
	CHECK(NextShutdownAnnouncement(3 * 3600 + 5) == 3 * 3600);
	CHECK(NextShutdownAnnouncement(3 * 3600) == 2 * 3600);
	CHECK(NextShutdownAnnouncement(3601) == 3600);
	CHECK(NextShutdownAnnouncement(3600) == 1800);
	CHECK(NextShutdownAnnouncement(1900) == 1800);
}

TEST_CASE("Shutdown times are formatted with the matching unit", "[shutdown]")
{
	auto check = [](uint32 seconds, const char* text, ShutdownTimeUnit unit)
	{
		const FormattedShutdownTime formatted = FormatShutdownTime(seconds);
		CHECK(formatted.time == text);
		CHECK(formatted.unit == unit);
	};

	check(7200, "2:00:00", shutdown_time_unit::Hours);
	check(3600, "1:00:00", shutdown_time_unit::Hours);
	check(1800, "30:00", shutdown_time_unit::Minutes);
	check(90, "1:30", shutdown_time_unit::Minutes);
	check(60, "1:00", shutdown_time_unit::Minute);
	check(45, "0:45", shutdown_time_unit::Seconds);
	check(5, "0:05", shutdown_time_unit::Seconds);
}

TEST_CASE("Shutdown delays parse from seconds, m:ss and h:mm:ss", "[shutdown]")
{
	uint32 seconds = 99;
	CHECK(ParseShutdownDelay("0", seconds));
	CHECK(seconds == 0);
	CHECK(ParseShutdownDelay("90", seconds));
	CHECK(seconds == 90);
	CHECK(ParseShutdownDelay("30:00", seconds));
	CHECK(seconds == 1800);
	CHECK(ParseShutdownDelay("1:05:30", seconds));
	CHECK(seconds == 3930);
	CHECK(ParseShutdownDelay("120:00", seconds));
	CHECK(seconds == 7200);

	CHECK_FALSE(ParseShutdownDelay("", seconds));
	CHECK_FALSE(ParseShutdownDelay("abc", seconds));
	CHECK_FALSE(ParseShutdownDelay("1:60", seconds));
	CHECK_FALSE(ParseShutdownDelay("1:60:00", seconds));
	CHECK_FALSE(ParseShutdownDelay("1::00", seconds));
	CHECK_FALSE(ParseShutdownDelay("1:00:00:00", seconds));
	CHECK_FALSE(ParseShutdownDelay("-5", seconds));
	CHECK_FALSE(ParseShutdownDelay("99999999", seconds));
	CHECK_FALSE(ParseShutdownDelay("169:00:00", seconds));
	CHECK(ParseShutdownDelay("168:00:00", seconds));
	CHECK(seconds == MaxShutdownDelaySeconds);
}
