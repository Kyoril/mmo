// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/clock.h"

#include <chrono>
#include <cstdio>
#include <string>

namespace mmo
{
	/// How long clients blend the sky from the old to the new time of day when a game master
	/// changes it, unless the command asks for a different duration.
	constexpr uint32 DefaultTimeOfDayTransitionMs = 8000;

	/// Upper bound for a requested time of day transition. Longer blends would leave the sky
	/// visibly out of step with the server's time for no benefit.
	constexpr uint32 MaxTimeOfDayTransitionMs = 60000;

	/// Returns the current UTC time of day of this machine's system clock, in milliseconds since
	/// midnight. This is the time of day the game uses when nobody has overridden it.
	inline GameTime GetSystemTimeOfDay()
	{
		const auto now = std::chrono::system_clock::now().time_since_epoch();
		const auto nowMs = static_cast<GameTime>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
		return nowMs % constants::OneDay;
	}

	/// Returns the signed shortest distance in milliseconds from one time of day to another, taking
	/// the wrap at midnight into account. The result is in (-12h, +12h]: an exact half-day distance
	/// resolves forwards, so the sun keeps moving in its natural direction.
	/// @param from Source time of day in milliseconds.
	/// @param to Destination time of day in milliseconds.
	inline int64 GetShortestTimeOfDayDelta(const GameTime from, const GameTime to)
	{
		const int64 day = static_cast<int64>(constants::OneDay);
		int64 delta = (static_cast<int64>(to % constants::OneDay) - static_cast<int64>(from % constants::OneDay)) % day;
		if (delta <= -day / 2)
		{
			delta += day;
		}
		else if (delta > day / 2)
		{
			delta -= day;
		}

		return delta;
	}

	/// Parses a time of day written as "HH", "HH:MM" or "HH:MM:SS" (24 hour clock).
	/// @param text The text to parse.
	/// @param out_timeOfDay Receives the parsed time of day in milliseconds since midnight.
	/// @returns true if the text was a valid time of day, false otherwise.
	inline bool ParseTimeOfDay(const std::string& text, GameTime& out_timeOfDay)
	{
		uint32 parts[3] = { 0, 0, 0 };
		size_t partCount = 0;
		size_t digits = 0;

		for (const char c : text)
		{
			if (c >= '0' && c <= '9')
			{
				if (++digits > 2)
				{
					return false;
				}

				parts[partCount] = parts[partCount] * 10 + static_cast<uint32>(c - '0');
			}
			else if (c == ':')
			{
				if (digits == 0 || partCount == 2)
				{
					return false;
				}

				++partCount;
				digits = 0;
			}
			else
			{
				return false;
			}
		}

		if (digits == 0)
		{
			return false;
		}

		if (parts[0] > 23 || parts[1] > 59 || parts[2] > 59)
		{
			return false;
		}

		out_timeOfDay = parts[0] * constants::OneHour + parts[1] * constants::OneMinute + parts[2] * constants::OneSecond;
		return true;
	}

	/// Formats a time of day as "HH:MM:SS".
	/// @param timeOfDay Time of day in milliseconds since midnight. Values past one day wrap.
	inline std::string FormatTimeOfDay(const GameTime timeOfDay)
	{
		const GameTime wrapped = timeOfDay % constants::OneDay;

		char buffer[16];
		std::snprintf(buffer, sizeof(buffer), "%02u:%02u:%02u",
			static_cast<uint32>(wrapped / constants::OneHour),
			static_cast<uint32>((wrapped / constants::OneMinute) % 60),
			static_cast<uint32>((wrapped / constants::OneSecond) % 60));
		return buffer;
	}
}
