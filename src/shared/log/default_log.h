// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "log.h"
#include "log_entry.h"

namespace mmo
{
	extern Log g_DefaultLog;

#if defined(MMO_LOG) || defined(MMO_LOG_FORMATTER_NAME)
#error Something went wrong with the log macros
#endif

#define MMO_LOG_FORMATTER_NAME _mmo_log_formatter_
#define MMO_LOG(level, message) \
	{ \
		::std::basic_ostringstream<char> MMO_LOG_FORMATTER_NAME; \
		MMO_LOG_FORMATTER_NAME << message; \
		::mmo::g_DefaultLog.signal()( \
		                                ::mmo::LogEntry(level, \
		                                        MMO_LOG_FORMATTER_NAME.str(), \
		                                        ::std::chrono::system_clock::now() \
		                                                 ) \
		                              ); \
	}
}
