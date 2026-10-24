// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/clock.h"

namespace mmo
{
	/// Hour of the given time of day on a 24-hour clock.
	/// @param timeOfDay Time of day in milliseconds since midnight. Values past one day wrap.
	/// @returns A value in [0, 23].
	inline uint32 GetHourOfDay(const GameTime timeOfDay)
	{
		return static_cast<uint32>((timeOfDay % constants::OneDay) / constants::OneHour);
	}

	/// Number of times a clock strikes at the given time of day: once at 1 o'clock, twelve times at
	/// noon and midnight (12-hour clock, as church bells count).
	/// @param timeOfDay Time of day in milliseconds since midnight. Values past one day wrap.
	/// @returns A value in [1, 12].
	inline uint32 GetClockStrikeCount(const GameTime timeOfDay)
	{
		const uint32 hour = GetHourOfDay(timeOfDay);
		return (hour + 11) % 12 + 1;
	}

	/// Value an hour-of-day trigger event carries for the given time of day: the hour on a 24-hour
	/// clock with midnight reported as 24, because trigger event data treats 0 as a wildcard.
	/// @param timeOfDay Time of day in milliseconds since midnight. Values past one day wrap.
	/// @returns A value in [1, 24].
	inline uint32 GetHourOfDayEventValue(const GameTime timeOfDay)
	{
		const uint32 hour = GetHourOfDay(timeOfDay);
		return hour == 0 ? 24 : hour;
	}

	/// Decides when a full hour has been reached, so it can be announced exactly once.
	///
	/// An hour is due while the time of day lies within the first minute after a full hour and that
	/// hour has not been announced yet. Watching a window instead of a crossing means a time of day
	/// that is *set* to a full hour (a game master's settime) is announced too, not only one that
	/// runs into it. Leaving the window re-arms the hour, so setting the same full hour again
	/// announces it again.
	class HourChime final
	{
	public:
		/// How long after a full hour it still counts as reached.
		static constexpr GameTime Window = constants::OneMinute;

	public:
		/// Feeds the current time of day.
		/// @param timeOfDay Time of day in milliseconds since midnight. Values past one day wrap.
		/// @param out_hour Receives the hour that was reached, in [0, 23], if one is due.
		/// @returns true if a full hour was reached and should be announced now.
		bool Update(const GameTime timeOfDay, uint32& out_hour)
		{
			const GameTime wrapped = timeOfDay % constants::OneDay;
			if (wrapped % constants::OneHour >= Window)
			{
				m_announcedHour = NoHour;
				return false;
			}

			const uint32 hour = static_cast<uint32>(wrapped / constants::OneHour);
			if (hour == m_announcedHour)
			{
				return false;
			}

			m_announcedHour = hour;
			out_hour = hour;
			return true;
		}

	private:
		static constexpr uint32 NoHour = 0xFFFFFFFF;

		uint32 m_announcedHour = NoHour;
	};
}
