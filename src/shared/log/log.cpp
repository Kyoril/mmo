// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "log.h"

namespace mmo
{
	Log::Log()
	{
	}

	Log::Signal &Log::signal()
	{
		return m_signal;
	}

	const Log::Signal &Log::signal() const
	{
		return m_signal;
	}

	Log::Formatter &Log::getFormatter()
	{
		return m_formatter;
	}
}
