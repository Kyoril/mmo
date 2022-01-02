// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "default_log_levels.h"

namespace mmo
{
	const LogLevel DebugLevel("Debug", log_importance::Low, log_color::Green);
	const LogLevel InfoLevel("Info", log_importance::Medium, log_color::White);
	const LogLevel WarningLevel("Warning", log_importance::Medium, log_color::Yellow);
	const LogLevel ErrorLevel("Error", log_importance::High, log_color::Red);
}
