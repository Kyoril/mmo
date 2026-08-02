// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "log.h"

#include "base/thread_checks.h"

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

	void Log::Emit(const LogEntry &entry)
	{
		if (IsMainThread())
		{
			m_signal(entry);
			return;
		}

		std::scoped_lock lock{ m_bufferMutex };
		m_buffered.push_back(entry);
	}

	void Log::FlushBuffered()
	{
		// Move the buffer out under the lock, emit without holding it so log slots
		// (console, log file) can themselves log without deadlocking.
		std::vector<LogEntry> pending;
		{
			std::scoped_lock lock{ m_bufferMutex };
			if (m_buffered.empty())
			{
				return;
			}
			pending.swap(m_buffered);
		}

		for (const LogEntry &entry : pending)
		{
			m_signal(entry);
		}
	}
}
