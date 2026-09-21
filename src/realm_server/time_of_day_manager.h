// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/non_copyable.h"
#include "base/signal.h"

#include <functional>

namespace mmo
{
	/// Owns the realm-wide game time of day.
	///
	/// By default the game time of day is the realm's UTC system time of day. A game master (or the
	/// REST api) can move it, which is stored as an offset to the system clock: the clock keeps
	/// running normally from the new value until the realm restarts or the time is changed again.
	/// The realm is the single authority; world nodes receive the absolute time of day from it and
	/// derive their own local offset, so their system clocks never need to agree with the realm's.
	class TimeOfDayManager final : public NonCopyable
	{
	public:
		/// Returns the current time of day of the system clock in milliseconds since midnight.
		typedef std::function<GameTime()> SystemTimeOfDayProvider;

	public:
		/// Fired after the time of day has been changed.
		/// @param timeOfDay The new time of day in milliseconds since midnight.
		/// @param transitionMs How long clients should blend towards the new time of day.
		signal<void(GameTime timeOfDay, uint32 transitionMs)> timeOfDayChanged;

	public:
		/// Creates the manager.
		/// @param systemTimeOfDay Source of the system time of day. Defaults to the UTC system clock
		///        and is only replaced by tests.
		explicit TimeOfDayManager(SystemTimeOfDayProvider systemTimeOfDay = nullptr);

	public:
		/// Gets the current game time of day in milliseconds since midnight.
		[[nodiscard]] GameTime GetTimeOfDay() const;

		/// Gets the current system time of day in milliseconds since midnight.
		[[nodiscard]] GameTime GetSystemTimeOfDay() const;

		/// Gets the offset between the system time of day and the game time of day in milliseconds,
		/// in [0, one day).
		[[nodiscard]] GameTime GetOffset() const { return m_offset; }

		/// Determines whether the time of day has been overridden since the realm started.
		[[nodiscard]] bool IsOverridden() const { return m_overridden; }

		/// Changes the game time of day and fires timeOfDayChanged.
		/// @param timeOfDay The new time of day in milliseconds since midnight. Values past one day wrap.
		/// @param transitionMs How long clients should blend towards the new time of day, clamped to
		///        MaxTimeOfDayTransitionMs.
		void SetTimeOfDay(GameTime timeOfDay, uint32 transitionMs);

		/// Returns to the system time of day and fires timeOfDayChanged.
		/// @param transitionMs How long clients should blend towards the new time of day, clamped to
		///        MaxTimeOfDayTransitionMs.
		void Reset(uint32 transitionMs);

	private:
		SystemTimeOfDayProvider m_systemTimeOfDay;
		GameTime m_offset = 0;
		bool m_overridden = false;
	};
}
