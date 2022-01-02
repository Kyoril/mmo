// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "log_stream_options.h"

namespace mmo
{
	LogStreamOptions::LogStreamOptions()
		: displayLogLevel(true)
		, displayImportance(true)
		, displayTime(false)
		, displayColor(false)
		, alwaysFlush(false)
		, minimumImportance(log_importance::Low)
	{
	}

	LogStreamOptions::LogStreamOptions(
	    bool displayLogLevel,
	    bool displayImportance,
	    bool displayTime,
	    bool displayColor,
	    bool alwaysFlush,
	    LogImportance minimumImportance)
		: displayLogLevel(displayLogLevel)
		, displayImportance(displayImportance)
		, displayTime(displayTime)
		, displayColor(displayColor)
		, alwaysFlush(alwaysFlush)
		, minimumImportance(minimumImportance)
	{
	}
}
