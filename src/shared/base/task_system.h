// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "macros.h"
#include "non_copyable.h"
#include "thread_checks.h"

namespace mmo
{
	/// @brief Completion counter that lets one thread wait for a batch of tasks.
	///
	/// Add() the number of expected completions (or pass it to the constructor), have each
	/// task call CountDown() when done, and Wait() blocks until the counter reaches zero.
	class TaskLatch final : public NonCopyable
	{
	public:
		/// @brief Constructs the latch with an initial number of expected completions.
		/// @param count Number of CountDown() calls Wait() should wait for.
		explicit TaskLatch(const size_t count = 0)
			: m_pending(count)
		{
		}

		/// @brief Adds expected completions. Must not be called after Wait() returned.
		/// @param count Number of additional CountDown() calls to wait for.
		void Add(const size_t count = 1)
		{
			std::scoped_lock lock{ m_mutex };
			m_pending += count;
		}

		/// @brief Signals one completion. Safe to call from any thread.
		void CountDown()
		{
			std::scoped_lock lock{ m_mutex };
			ASSERT(m_pending > 0);
			if (--m_pending == 0)
			{
				m_condition.notify_all();
			}
		}

		/// @brief Blocks until all expected completions have been signaled.
		void Wait()
		{
			std::unique_lock lock{ m_mutex };
			m_condition.wait(lock, [this]() { return m_pending == 0; });
		}

	private:
		std::mutex m_mutex;

		std::condition_variable m_condition;

		size_t m_pending;
	};

	/// @brief Engine-wide worker thread pool for fork-join style CPU parallelism.
	///
	/// Threading model: the main thread owns the frame and fans work out at explicit points
	/// ("parallel islands"), joining before it proceeds. Jobs must be self-contained CPU work —
	/// they must never touch signal<>, Lua, GraphicsDevice, or the logging system (all of which
	/// are main-thread-only by contract, see docs/threading.md).
	///
	/// The system is inert until Initialize() is called (servers and tools never call it and are
	/// unaffected): ParallelFor degrades to a serial loop and Dispatch executes inline.
	class TaskSystem final : public NonCopyable
	{
	public:
		/// @brief Returns the global task system instance.
		static TaskSystem& Get();

		/// @brief Starts the worker pool. Must be called from the main thread.
		/// @param threadCount Number of worker threads; 0 picks a default based on
		///                    hardware_concurrency, reserving cores for the main thread
		///                    and background (streaming/audio) threads.
		void Initialize(size_t threadCount = 0);

		/// @brief Stops all workers after draining queued jobs. Must be called from the main thread.
		void Shutdown();

		/// @brief Stops the worker pool if it is still running.
		/// @remarks Shutdown() is the intended way to stop the pool, but the instance is a function
		///          local static and therefore outlives any startup path that bails out before
		///          reaching it. Destroying joinable threads terminates the process, which would
		///          turn an orderly "cannot start" into a crash report.
		~TaskSystem();

		/// @brief Returns whether the worker pool is running.
		[[nodiscard]] bool IsInitialized() const { return !m_workers.empty(); }

		/// @brief Returns the number of worker threads (0 if not initialized).
		[[nodiscard]] size_t GetWorkerCount() const { return m_workers.size(); }

		/// @brief Returns true if called from the thread that called Initialize(), or if the
		///        system was never initialized (so main-thread asserts stay inert in servers/tools).
		[[nodiscard]] bool IsMainThread() const;

		/// @brief Queues a fire-and-forget job on the worker pool.
		/// @remark Executes the job inline when the system is not initialized.
		/// @param job The job to execute.
		void Dispatch(std::function<void()> job);

		/// @brief Runs body over the range [0, count) in parallel chunks and blocks until done.
		///
		/// The calling thread participates in chunk processing, so this never deadlocks even
		/// when all workers are busy. Degrades to a plain serial loop when count <= grainSize
		/// or the system is not initialized.
		///
		/// @param count Number of elements to process.
		/// @param grainSize Minimum number of elements per chunk (>= 1).
		/// @param body Callable invoked as body(begin, end) for disjoint sub-ranges.
		void ParallelFor(size_t count, size_t grainSize, const std::function<void(size_t begin, size_t end)>& body);

	private:
		TaskSystem() = default;

		/// @brief Worker thread main loop.
		/// @param workerIndex Index of this worker, used for thread naming.
		void WorkerRun(size_t workerIndex);

	private:
		/// Worker threads. Non-empty exactly while the system is initialized.
		std::vector<std::thread> m_workers;

		/// Guards m_jobs and m_stopping.
		std::mutex m_queueMutex;

		/// Signals workers that a job was queued or shutdown was requested.
		std::condition_variable m_queueCondition;

		/// Pending fire-and-forget jobs.
		std::deque<std::function<void()>> m_jobs;

		/// Set during Shutdown; workers drain the queue and exit.
		bool m_stopping = false;

		/// Id of the thread that called Initialize().
		std::thread::id m_mainThreadId;
	};

}
