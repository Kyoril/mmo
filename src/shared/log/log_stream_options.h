// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "log_level.h"

namespace mmo
{
	struct LogStreamOptions
	{
		bool displayLogLevel;
		bool displayImportance;
		bool displayTime;
		bool displayColor;
		bool alwaysFlush;
		LogImportance minimumImportance;


		LogStreamOptions();
		explicit LogStreamOptions(
		    bool displayLogLevel,
		    bool displayImportance,
		    bool displayTime,
		    bool displayColor,
		    bool alwaysFlush,
		    LogImportance minimumImportance);
	};
}
