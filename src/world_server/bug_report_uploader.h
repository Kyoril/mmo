// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mmo
{
	/// Uploads bug reports to the central bug API from a worker thread.
	///
	/// The world server is single-threaded and its game loop must never wait on HTTP, so reports
	/// are queued here and sent one by one by a dedicated thread. Results are handed back to the
	/// owning thread through the poster (the io_service), which is also where they are logged:
	/// the worker thread itself never logs, because servers do not buffer off-thread log entries.
	///
	/// Reports live in memory only. They are lost when the server stops or when the API stays
	/// unreachable for all retry attempts; that trade-off was chosen over a disk spool.
	class BugReportUploader final : public NonCopyable
	{
	public:
		/// Sends one report body. Returns the HTTP status, or 0 for a network level failure
		/// (in which case error describes it). Called on the worker thread.
		typedef std::function<uint32(const std::string& body, std::string& error)> Transport;

		/// Runs a function on the owning thread.
		typedef std::function<void(std::function<void()>)> Poster;

		/// What happened to an upload attempt.
		struct Outcome
		{
			enum Kind
			{
				/// The report was stored by the API.
				Delivered,
				/// One attempt failed; the report will be retried.
				AttemptFailed,
				/// The report was given up on (4xx, out of attempts, or queue overflow).
				Dropped
			};

			Kind kind = Delivered;
			/// HTTP status of the attempt, 0 for network errors and overflow.
			uint32 status = 0;
			/// Human readable detail for the log.
			std::string detail;
		};

		typedef std::function<void(const Outcome&)> OutcomeHandler;

	public:
		/// @param transport Sends a single report.
		/// @param poster Hands outcomes back to the owning thread.
		/// @param onOutcome Called on the owning thread for every outcome.
		/// @param retryDelaysMs Delay before each retry; attempts = retries + 1.
		/// @param maxQueueSize Reports kept waiting; on overflow the oldest is dropped.
		BugReportUploader(Transport transport, Poster poster, OutcomeHandler onOutcome,
			std::vector<uint32> retryDelaysMs = { 5000, 30000, 120000, 120000 }, size_t maxQueueSize = 200);

		~BugReportUploader();

	public:
		/// Starts the worker thread.
		void Start();

		/// Stops the worker thread and drops every report still queued. Safe to call twice.
		void Stop();

		/// Queues a report body.
		/// @returns false if the queue was full and the oldest report had to be dropped.
		bool Enqueue(std::string body);

		/// Number of reports waiting (not counting the one being sent).
		[[nodiscard]] size_t GetQueueSize() const;

	private:
		void Run();

		void Report(Outcome outcome);

	private:
		Transport m_transport;
		Poster m_poster;
		OutcomeHandler m_onOutcome;
		std::vector<uint32> m_retryDelaysMs;
		size_t m_maxQueueSize;

		mutable std::mutex m_mutex;
		std::condition_variable m_wakeUp;
		std::deque<std::string> m_queue;
		bool m_stopping = false;
		std::thread m_thread;
	};
}
