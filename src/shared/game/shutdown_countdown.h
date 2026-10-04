// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// Value of a ShutdownCountdown packet announcing that a pending shutdown was cancelled.
	constexpr uint32 ShutdownCountdownCancelled = 0xFFFFFFFF;

	/// Longest delay a realm shutdown may be scheduled with (one week).
	constexpr uint32 MaxShutdownDelaySeconds = 7 * 24 * 3600;

	/// Returns the next point (in remaining seconds) at which a pending shutdown is announced,
	/// strictly below remainingSeconds, or 0 when the next event is the shutdown itself.
	/// Marks: every full hour above one hour, then 30:00, 15:00, 10:00, every minute from 5:00
	/// to 1:00, then 0:45, 0:30 and 0:15.
	uint32 NextShutdownAnnouncement(uint32 remainingSeconds);

	/// The unit word a formatted shutdown time is shown with.
	namespace shutdown_time_unit
	{
		enum Type
		{
			/// One hour or more, formatted h:mm:ss.
			Hours,
			/// More than one minute, formatted m:ss.
			Minutes,
			/// Exactly one minute, formatted 1:00.
			Minute,
			/// Less than one minute, formatted 0:ss.
			Seconds,
		};
	}

	typedef shutdown_time_unit::Type ShutdownTimeUnit;

	/// A remaining shutdown time as shown to players.
	struct FormattedShutdownTime
	{
		/// The clock text, e.g. "4:00" or "1:00:00".
		String time;
		/// Which unit word belongs after the time.
		ShutdownTimeUnit unit;
	};

	/// Formats a remaining shutdown time for display.
	FormattedShutdownTime FormatShutdownTime(uint32 seconds);

	/// Parses a shutdown delay given as seconds ("90"), m:ss ("30:00") or h:mm:ss ("1:30:00").
	/// The leading component is unbounded, the others must be below 60.
	/// @returns false if the text is malformed or exceeds MaxShutdownDelaySeconds.
	bool ParseShutdownDelay(const String& text, uint32& out_seconds);
}
