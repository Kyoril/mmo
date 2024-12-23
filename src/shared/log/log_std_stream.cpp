// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "log_std_stream.h"
#include "log_entry.h"
#include "log_level.h"
#include "base/console.h"
#include <ctime>
#include <iomanip>

namespace mmo
{
	static Console::Color logToConsoleColor(LogColor from)
	{
		switch (from)
		{
		case log_color::White:
			return Console::White;
		case log_color::Grey:
			return Console::DarkGray;
		case log_color::Black:
			return Console::Black;
		case log_color::Red:
			return Console::Red;
		case log_color::Green:
			return Console::Green;
		case log_color::Blue:
			return Console::Blue;
		case log_color::Yellow:
			return Console::Yellow;
		case log_color::Purple:
			return Console::Magenta;
		}

		return Console::White;
	}


	const LogStreamOptions g_DefaultConsoleLogOptions(
	    false, //bool displayLogLevel;
	    false, //bool displayImportance;
	    true, //bool displayTime;
	    true, //bool displayColor;
	    true, //bool alwaysFlush;
	    log_importance::Low //LogImportance minimumImportance
	);

	const LogStreamOptions g_DefaultFileLogOptions(
	    true, //bool displayLogLevel;
	    true, //bool displayImportance;
	    true, //bool displayTime;
	    false, //bool displayColor;
	    false, //bool alwaysFlush;
	    log_importance::Low //LogImportance minimumImportance
	);


	void printLogEntry(
	    std::basic_ostream<char> &stream,
	    const LogEntry &entry,
	    const LogStreamOptions &options)
	{
		const LogLevel &level = *entry.level;

		Console::Color oldColor = Console::White;
		if (options.displayColor)
		{
			oldColor = Console::getTextColor();
			Console::setTextColor(logToConsoleColor(level.color));
		}

		if (options.displayImportance &&
		        level.importance == log_importance::High)
		{
			stream
			        << stream.widen('!')
			        << stream.widen(' ');
		}

		if (options.displayTime)
		{
			const auto timeT = std::chrono::system_clock::to_time_t(entry.time);
			stream
			        << std::put_time(std::localtime(&timeT), "%Y-%b-%d %H:%M:%S")
			        << stream.widen(' ');
		}

		if (options.displayLogLevel)
		{
			stream
			        << stream.widen('[')
			        << level.name
			        << stream.widen(']')
			        << stream.widen(' ');
		}

		stream
		        << entry.message
		        << '\n';

		if (options.alwaysFlush)
		{
			stream.flush();
		}

		if (options.displayColor)
		{
			Console::setTextColor(oldColor);
		}
	}
}
