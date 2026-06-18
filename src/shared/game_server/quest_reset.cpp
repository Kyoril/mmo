// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "quest_reset.h"

namespace mmo
{
	namespace
	{
		constexpr uint64 SecondsPerHour = 3600;
		constexpr uint64 SecondsPerDay = 86400;
		constexpr uint64 DaysPerWeek = 7;

		// 1970-01-01 (unix epoch) was a Thursday. With Sunday = 0, that is weekday 4.
		constexpr uint64 EpochWeekdayOffset = 4;

		QuestResetConfig s_questResetConfig;
	}

	void SetQuestResetConfig(const QuestResetConfig& config)
	{
		s_questResetConfig = config;
	}

	const QuestResetConfig& GetQuestResetConfig()
	{
		return s_questResetConfig;
	}

	uint64 GetNextDailyResetTime(const uint64 nowUnix)
	{
		const uint64 hour = (s_questResetConfig.dailyResetHour % 24);
		const uint64 dayStart = nowUnix - (nowUnix % SecondsPerDay);

		uint64 reset = dayStart + hour * SecondsPerHour;
		if (reset <= nowUnix)
		{
			reset += SecondsPerDay;
		}

		return reset;
	}

	uint64 GetNextWeeklyResetTime(const uint64 nowUnix)
	{
		const uint64 hour = (s_questResetConfig.weeklyResetHour % 24);
		const uint64 targetWeekday = (s_questResetConfig.weeklyResetWeekday % DaysPerWeek);

		const uint64 daysSinceEpoch = nowUnix / SecondsPerDay;
		const uint64 currentWeekday = (daysSinceEpoch + EpochWeekdayOffset) % DaysPerWeek;

		const uint64 dayStart = daysSinceEpoch * SecondsPerDay;
		const uint64 dayDiff = (targetWeekday + DaysPerWeek - currentWeekday) % DaysPerWeek;

		uint64 reset = dayStart + dayDiff * SecondsPerDay + hour * SecondsPerHour;
		if (reset <= nowUnix)
		{
			reset += DaysPerWeek * SecondsPerDay;
		}

		return reset;
	}
}
