// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "default_log_levels.h"

namespace mmo
{
	const LogLevel g_DebugLevel("Debug", log_importance::Low, log_color::Green);
	const LogLevel g_InfoLevel("Info", log_importance::Medium, log_color::White);
	const LogLevel g_WarningLevel("Warning", log_importance::Medium, log_color::Yellow);
	const LogLevel g_ErrorLevel("Error", log_importance::High, log_color::Red);
}
