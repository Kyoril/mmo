// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "log_entry.h"

namespace mmo
{
	LogEntry::LogEntry()
		: level(nullptr)
	{
	}

	LogEntry::LogEntry(
	    const LogLevel &level,
	    String message,
	    const LogTime &time)
		: level(&level)
		, message(std::move(message))
		, time(time)
	{
	}

	LogEntry::LogEntry(const LogEntry &other)
		: level(other.level)
		, message(other.message)
		, time(other.time)
	{
	}

	LogEntry::LogEntry(LogEntry  &&other)
	{
		swap(other);
	}

	LogEntry &LogEntry::operator = (LogEntry other)
	{
		swap(other);
		return *this;
	}

	void LogEntry::swap(LogEntry &other)
	{
		std::swap(level, other.level);
		std::swap(message, other.message);
		std::swap(time, other.time);
	}


	void swap(LogEntry &left, LogEntry &right)
	{
		left.swap(right);
	}
}
