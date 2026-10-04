// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "shutdown_countdown.h"

#include <cstdio>
#include <vector>

namespace mmo
{
	namespace
	{
		constexpr uint32 secondsPerHour = 3600;

		/// Announcement marks at or below one hour, descending.
		constexpr uint32 fixedMarks[] = { 1800, 900, 600, 300, 240, 180, 120, 60, 45, 30, 15 };
	}

	uint32 NextShutdownAnnouncement(const uint32 remainingSeconds)
	{
		if (remainingSeconds > secondsPerHour)
		{
			// The largest full hour strictly below the remaining time.
			return ((remainingSeconds - 1) / secondsPerHour) * secondsPerHour;
		}

		for (const uint32 mark : fixedMarks)
		{
			if (mark < remainingSeconds)
			{
				return mark;
			}
		}

		return 0;
	}

	FormattedShutdownTime FormatShutdownTime(const uint32 seconds)
	{
		char buffer[32];

		if (seconds >= secondsPerHour)
		{
			std::snprintf(buffer, sizeof(buffer), "%u:%02u:%02u", seconds / secondsPerHour, (seconds / 60) % 60, seconds % 60);
			return { buffer, shutdown_time_unit::Hours };
		}

		std::snprintf(buffer, sizeof(buffer), "%u:%02u", seconds / 60, seconds % 60);
		if (seconds > 60)
		{
			return { buffer, shutdown_time_unit::Minutes };
		}

		if (seconds == 60)
		{
			return { buffer, shutdown_time_unit::Minute };
		}

		return { buffer, shutdown_time_unit::Seconds };
	}

	bool ParseShutdownDelay(const String& text, uint32& out_seconds)
	{
		std::vector<uint32> parts;
		uint64 current = 0;
		size_t digits = 0;

		for (const char c : text)
		{
			if (c == ':')
			{
				if (digits == 0)
				{
					return false;
				}

				parts.push_back(static_cast<uint32>(current));
				current = 0;
				digits = 0;
				continue;
			}

			if (c < '0' || c > '9')
			{
				return false;
			}

			// Seven digits already exceed a week in seconds; stop before the value can overflow.
			if (++digits > 7)
			{
				return false;
			}

			current = current * 10 + static_cast<uint64>(c - '0');
		}

		if (digits == 0)
		{
			return false;
		}

		parts.push_back(static_cast<uint32>(current));
		if (parts.size() > 3)
		{
			return false;
		}

		// Every component after the first is a minutes or seconds field.
		for (size_t i = 1; i < parts.size(); ++i)
		{
			if (parts[i] >= 60)
			{
				return false;
			}
		}

		uint64 total = 0;
		for (const uint32 part : parts)
		{
			total = total * 60 + part;
		}

		if (total > MaxShutdownDelaySeconds)
		{
			return false;
		}

		out_seconds = static_cast<uint32>(total);
		return true;
	}
}
