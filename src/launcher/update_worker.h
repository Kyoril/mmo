// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "launcher_model.h"

#include "base/typedefs.h"
#include "base/non_copyable.h"

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace mmo
{
	struct UpdateWorkerConfig
	{
		std::string sourceUrl;
		std::string outputDir = "./";
		size_t concurrency = 4;
		bool selfUpdateEnabled = true;
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

		/// Signals the run to stop and joins the thread. Safe to call when not started.
		void Stop();

	private:
		void Run();

		LauncherModel& m_model;
		UpdateWorkerConfig m_config;

		std::thread m_thread;
		std::atomic<bool> m_shouldQuit{ false };

		/// Total bytes to write, known only after the prepare step completes.
		std::atomic<std::uintmax_t> m_updateSize{ 0 };
		std::atomic<std::uintmax_t> m_updated{ 0 };

		friend struct ModelProgressHandler;
	};
}
