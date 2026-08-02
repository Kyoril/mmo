// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/signal.h"
#include "log_level.h"
#include "log_entry.h"

#include <mutex>
#include <sstream>
#include <vector>

namespace mmo
{
	class Log
	{
	public:

		typedef mmo::signal<void (const LogEntry &)> Signal;
		typedef std::basic_ostringstream<char> Formatter;


		Log();
		Signal &signal();
		const Signal &signal() const;
		Formatter &getFormatter();

		/// @brief Thread-safe entry point used by the log macros.
		///
		/// The log signal — like every signal — is main-thread-only. Entries logged from the
		/// main thread (or in processes that never initialize the TaskSystem) are emitted
		/// directly. Entries logged from any other thread (TaskSystem workers, the streaming
		/// thread) are buffered and emitted by the next FlushBuffered() call on the main thread.
		void Emit(const LogEntry &entry);

		/// @brief Emits all buffered off-thread entries through the signal. Call once per
		///        frame from the main thread (the client event loop does this).
		void FlushBuffered();

	private:

		Signal m_signal;
		Formatter m_formatter;

		/// Guards m_buffered.
		std::mutex m_bufferMutex;

		/// Entries logged from non-main threads, waiting for FlushBuffered.
		std::vector<LogEntry> m_buffered;
	};
}
