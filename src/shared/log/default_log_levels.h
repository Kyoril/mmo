// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "log_level.h"
#include "default_log.h"

namespace mmo
{
	//For events that are useful for developers
	extern const LogLevel g_DebugLevel;

	//For common events
	// - Connected
	// - User signed in
	extern const LogLevel g_InfoLevel;

	//For unexpected events
	// - optional but useful setting not found
	// - unexpected user input
	extern const LogLevel g_WarningLevel;

	//For runtime errors
	// - File not found
	// - Connection lost
	extern const LogLevel g_ErrorLevel;


#define DLOG(message) MMO_LOG(::mmo::g_DebugLevel, message)
#define ILOG(message) MMO_LOG(::mmo::g_InfoLevel, message)
#define WLOG(message) MMO_LOG(::mmo::g_WarningLevel, message)
#define ELOG(message) MMO_LOG(::mmo::g_ErrorLevel, message)
}
