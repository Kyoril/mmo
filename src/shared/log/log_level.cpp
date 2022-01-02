// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "log_level.h"
#include <utility>

namespace mmo
{
	LogLevel::LogLevel()
	{
	}

	LogLevel::LogLevel(std::string name, LogImportance importance, LogColor color)
		: name(std::move(name))
		, importance(importance)
		, color(color)
	{
	}
}
