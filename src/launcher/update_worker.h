// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "launcher_model.h"

#include "base/typedefs.h"
#include "base/non_copyable.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace mmo
{
	struct UpdateWorkerConfig
	{
		std::string sourceUrl;
		std::string outputDir = "./";
		/// Parallel downloads. Each one keeps its own connection alive, so this mostly
		/// pays off for many small files, where the round trip dominates.
		size_t concurrency = 8;

		/// Background connections that download small files ahead of time with HTTP
		/// pipelining. Zero disables it.
		uint32 pipelineConnections = 4;

		/// Requests sent ahead on each pipelined connection.
		uint32 pipelineDepth = 16;
		bool selfUpdateEnabled = true;

		/// How long the connection may stall before a download attempt is given up.
		std::chrono::milliseconds inactivityTimeout{ 30000 };

		/// How often a single file is requested before the update fails.
		uint32 maxDownloadAttempts = 6;
	};

	/// Runs the update on a background thread and reports progress through the model.
	///
	/// The worker never touches the UI. Its only outputs are the model and the two
	/// callbacks below, which the platform marshals onto the UI thread.
	class UpdateWorker final : public NonCopyable
	{
	public:
		UpdateWorker(LauncherModel& model, UpdateWorkerConfig config);
		~UpdateWorker() override;

		/// Invoked on the worker thread when a launcher self-update has been applied
		/// and the replacement process has been spawned; this instance should close.
		std::function<void()> onSelfUpdateFinished;

		/// Invoked on the worker thread once the run has ended, successfully or not.
		std::function<void()> onFinished;

		void Start();

		/// Asks the run to stop as soon as possible and returns immediately. Running
		/// downloads are aborted, so this is safe to call the moment the user closes the
		/// window. Callable from any thread.
		void RequestStop();

		/// Asks the run to stop and waits up to `timeout` for it to end. Returns true if
		/// the worker has ended (or never started) and the thread was joined. On false
		/// the thread is still running and still uses the model, so the caller must not
		/// destroy either; ending the process is the only safe way out.
		bool Stop(std::chrono::milliseconds timeout = std::chrono::milliseconds::max());

	private:
		void Run();

		/// Ends a run: notifies onFinished and wakes up Stop.
		void Finish();

		/// Called for every progress report of a file download, from any worker thread.
		void OnFileProgress(const std::string& name, std::uintmax_t size, std::uintmax_t loaded);

		/// Called whenever the prepare step starts checking a local file.
		void OnCheckLocalCopy();

		/// Pushes the download counters into the model. m_progressMutex must be held.
		void PublishDownloadProgress();

		LauncherModel& m_model;
		UpdateWorkerConfig m_config;

		std::thread m_thread;
		std::atomic<bool> m_shouldQuit{ false };

		std::mutex m_finishedMutex;
		std::condition_variable m_finishedCondition;
		bool m_finished = false;

		struct FileProgress
		{
			std::uintmax_t loaded = 0;
			bool done = false;
		};

		/// Guards everything below. Progress arrives from several download threads.
		std::mutex m_progressMutex;

		/// Per-file high-water marks. The updater may report the same file as complete
		/// more than once and restarts it from zero after a failed attempt, so only the
		/// growth past the previous maximum counts towards the total.
		std::unordered_map<std::string, FileProgress> m_fileProgress;

		/// Totals, known only after the prepare step completes.
		std::uintmax_t m_updateSize = 0;
		std::uintmax_t m_fileCount = 0;

		std::uintmax_t m_updated = 0;
		std::uintmax_t m_filesDone = 0;
		std::uintmax_t m_filesChecked = 0;

		friend struct ModelProgressHandler;
	};
}
