// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include <chrono>

namespace mmo
{
	struct LogLevel;

	typedef std::chrono::time_point<std::chrono::system_clock> LogTime;


	class LogEntry
	{
	public:

		const LogLevel *level;
		String message;
		LogTime time;


		LogEntry();
		explicit LogEntry(
		    const LogLevel &level,
		    String message,
		    const LogTime &time);
		LogEntry(const LogEntry &other);
		LogEntry(LogEntry  &&other);
		LogEntry &operator = (LogEntry other);
		void swap(LogEntry &other);
	};


	void swap(LogEntry &left, LogEntry &right);
}
