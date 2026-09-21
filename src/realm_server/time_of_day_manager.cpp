// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "time_of_day_manager.h"

#include "base/clock.h"
#include "game/time_of_day.h"
#include "log/default_log_levels.h"

#include <algorithm>

namespace mmo
{
	TimeOfDayManager::TimeOfDayManager(SystemTimeOfDayProvider systemTimeOfDay)
		: m_systemTimeOfDay(std::move(systemTimeOfDay))
	{
		if (!m_systemTimeOfDay)
		{
			m_systemTimeOfDay = []() { return mmo::GetSystemTimeOfDay(); };
		}
	}

	GameTime TimeOfDayManager::GetTimeOfDay() const
	{
		return (GetSystemTimeOfDay() + m_offset) % constants::OneDay;
	}

	GameTime TimeOfDayManager::GetSystemTimeOfDay() const
	{
		return m_systemTimeOfDay() % constants::OneDay;
	}

	void TimeOfDayManager::SetTimeOfDay(const GameTime timeOfDay, const uint32 transitionMs)
	{
		const GameTime target = timeOfDay % constants::OneDay;
		const GameTime systemTime = GetSystemTimeOfDay();

		m_offset = (target + constants::OneDay - systemTime) % constants::OneDay;
		m_overridden = true;

		ILOG("Time of day set to " << FormatTimeOfDay(target) << " (offset to system time: " << m_offset << " ms)");
		timeOfDayChanged(target, std::min(transitionMs, MaxTimeOfDayTransitionMs));
	}

	void TimeOfDayManager::Reset(const uint32 transitionMs)
	{
		m_offset = 0;
		m_overridden = false;

		const GameTime timeOfDay = GetTimeOfDay();
		ILOG("Time of day reset to system time " << FormatTimeOfDay(timeOfDay));
		timeOfDayChanged(timeOfDay, std::min(transitionMs, MaxTimeOfDayTransitionMs));
	}
}
