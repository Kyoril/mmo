// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include <string>

namespace mmo
{
	struct log_importance
	{
		enum Enum
		{
			Low,
			Medium,
			High
		};
	};

	typedef log_importance::Enum LogImportance;


	struct log_color
	{
		enum Enum
		{
			White,
			Grey,
			Black,
			Red,
			Green,
			Blue,
			Yellow,
			Purple
		};
	};

	typedef log_color::Enum LogColor;


	struct LogLevel
	{
		std::string name;
		LogImportance importance;
		LogColor color;

		LogLevel();
		explicit LogLevel(std::string name, LogImportance importance, LogColor color);
	};
}
