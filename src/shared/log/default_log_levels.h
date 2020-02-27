// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "log_level.h"
#include "default_log.h"

namespace mmo
{
	//For events that are useful for developers
	extern const LogLevel DebugLevel;

	//For common events
	// - Connected
	// - User signed in
	extern const LogLevel InfoLevel;

	//For unexpected events
	// - optional but useful setting not found
	// - unexpected user input
	extern const LogLevel WarningLevel;

	//For runtime errors
	// - File not found
	// - Connection lost
	extern const LogLevel ErrorLevel;


#define DLOG(message) MMO_LOG(::mmo::DebugLevel, message)
#define ILOG(message) MMO_LOG(::mmo::InfoLevel, message)
#define WLOG(message) MMO_LOG(::mmo::WarningLevel, message)
#define ELOG(message) MMO_LOG(::mmo::ErrorLevel, message)
}
