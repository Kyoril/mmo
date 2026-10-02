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
		///
		/// In a process that never initializes the TaskSystem every thread counts as the main
		/// thread (nav_builder's workers, the servers' database thread), so direct emission is
		/// serialized: two threads emitting at once raced on the signal's non-atomic reference
		/// counts and corrupted the heap, which is what made nav_builder crash at random.
		/// Only emission is serialized: connecting to or disconnecting from signal() while
		/// another thread logs is still a race, so connect before the threads start and
		/// disconnect after they stop.
		void Emit(const LogEntry &entry);

		/// @brief Emits all buffered off-thread entries through the signal. Call once per
		///        frame from the main thread (the client event loop does this).
		void FlushBuffered();

	private:

		Signal m_signal;
		Formatter m_formatter;

		/// Serializes emission through m_signal. Recursive because a slot may log itself.
		std::recursive_mutex m_emitMutex;

		/// Guards m_buffered.
		std::mutex m_bufferMutex;

		/// Entries logged from non-main threads, waiting for FlushBuffered.
		std::vector<LogEntry> m_buffered;
	};
}
