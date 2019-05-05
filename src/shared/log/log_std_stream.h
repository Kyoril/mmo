// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "log_stream_options.h"

namespace mmo
{
	class LogEntry;


	extern const LogStreamOptions g_DefaultConsoleLogOptions;
	extern const LogStreamOptions g_DefaultFileLogOptions;


	void printLogEntry(
	    std::basic_ostream<char> &stream,
	    const LogEntry &entry,
	    const LogStreamOptions &options);
}
