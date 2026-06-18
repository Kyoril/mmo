// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// Configuration for global daily/weekly quest resets. All times are interpreted in UTC
	/// ("server time"). This lives in the shared game_server layer so that GamePlayerS can compute
	/// reset boundaries without depending on a concrete server's configuration; the world server
	/// pushes the loaded values via SetQuestResetConfig() at startup.
	struct QuestResetConfig
	{
		/// Hour of day [0, 23] at which daily quests reset.
		uint32 dailyResetHour = 3;

		/// Weekday [0 = Sunday, 6 = Saturday] on which weekly quests reset.
		uint32 weeklyResetWeekday = 3;	// Wednesday

		/// Hour of day [0, 23] at which weekly quests reset.
		uint32 weeklyResetHour = 3;
	};

	/// Overrides the process-wide quest reset configuration.
	/// @param config The new configuration to use.
	void SetQuestResetConfig(const QuestResetConfig& config);

	/// @returns The current process-wide quest reset configuration.
	const QuestResetConfig& GetQuestResetConfig();

	/// Computes the next daily reset boundary strictly after the given time.
	/// @param nowUnix The current time as a unix timestamp in seconds.
	/// @returns The next daily reset time as a unix timestamp in seconds.
	uint64 GetNextDailyResetTime(uint64 nowUnix);

	/// Computes the next weekly reset boundary strictly after the given time.
	/// @param nowUnix The current time as a unix timestamp in seconds.
	/// @returns The next weekly reset time as a unix timestamp in seconds.
	uint64 GetNextWeeklyResetTime(uint64 nowUnix);
}
