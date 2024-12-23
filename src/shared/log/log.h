// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/signal.h"
#include "log_level.h"
#include <sstream>

namespace mmo
{
	class LogEntry;


	class Log
	{
	public:

		typedef mmo::signal<void (const LogEntry &)> Signal;
		typedef std::basic_ostringstream<char> Formatter;


		Log();
		Signal &signal();
		const Signal &signal() const;
		Formatter &getFormatter();

	private:

		Signal m_signal;
		Formatter m_formatter;
	};
}
