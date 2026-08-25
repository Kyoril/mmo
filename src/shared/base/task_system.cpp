// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "task_system.h"

#include "profiler.h"

#include <algorithm>
#include <memory>
#include <string>

#ifdef _WIN32
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	include <Windows.h>
#endif

namespace mmo
{
	namespace
	{
		/// @brief Names the calling thread for debuggers and profilers. No-op where unsupported.
		void SetCurrentOsThreadName(const std::string& name)
		{
#ifdef _WIN32
			const std::wstring wideName{ name.begin(), name.end() };
			::SetThreadDescription(::GetCurrentThread(), wideName.c_str());
#else
			(void)name;
#endif
		}
	}

	bool IsMainThread()
	{
		return TaskSystem::Get().IsMainThread();
	}

	TaskSystem& TaskSystem::Get()
	{
		static TaskSystem instance;
		return instance;
	}

	void TaskSystem::Initialize(size_t threadCount)
	{
		ASSERT(!IsInitialized());

		m_mainThreadId = std::this_thread::get_id();

		if (threadCount == 0)
		{
			// Reserve one core for the main thread and one for background threads
			// (streaming worker, FMOD, OS).
			const size_t hardware = std::thread::hardware_concurrency();
			threadCount = std::clamp<size_t>(hardware > 2 ? hardware - 2 : 2, 2, 8);
		}

		{
			std::scoped_lock lock{ m_queueMutex };
			m_stopping = false;
		}

		m_workers.reserve(threadCount);
		for (size_t i = 0; i < threadCount; ++i)
		{
			m_workers.emplace_back([this, i]() { WorkerRun(i); });
		}
	}

	TaskSystem::~TaskSystem()
	{
		Shutdown();
	}

	void TaskSystem::Shutdown()
	{
		if (!IsInitialized())
		{
			return;
		}

		ASSERT(IsMainThread());

		{
			std::scoped_lock lock{ m_queueMutex };
			m_stopping = true;
		}
		m_queueCondition.notify_all();

		for (auto& worker : m_workers)
		{
			worker.join();
		}
		m_workers.clear();

		ASSERT(m_jobs.empty());
	}

	bool TaskSystem::IsMainThread() const
	{
		if (m_mainThreadId == std::thread::id())
		{
			// Never initialized: stay inert so servers and tools are unaffected.
			return true;
		}

		return std::this_thread::get_id() == m_mainThreadId;
	}

	void TaskSystem::Dispatch(std::function<void()> job)
	{
		ASSERT(job);

		if (!IsInitialized())
		{
			job();
			return;
		}

		{
			std::scoped_lock lock{ m_queueMutex };
			ASSERT(!m_stopping);
			m_jobs.push_back(std::move(job));
		}
		m_queueCondition.notify_one();
	}

	void TaskSystem::ParallelFor(const size_t count, size_t grainSize, const std::function<void(size_t begin, size_t end)>& body)
	{
		if (count == 0)
		{
			return;
		}

		grainSize = std::max<size_t>(grainSize, 1);

		if (!IsInitialized() || count <= grainSize)
		{
			body(0, count);
			return;
		}

		// Shared between the calling thread and the helper jobs. Kept alive by shared_ptr so
		// helpers that only start after the call already returned remain harmless no-ops.
		struct SharedState
		{
			std::atomic<size_t> nextChunk{ 0 };
			std::atomic<size_t> chunksDone{ 0 };
			size_t chunkCount = 0;
			size_t grain = 0;
			size_t count = 0;
			const std::function<void(size_t, size_t)>* body = nullptr;
			std::mutex mutex;
			std::condition_variable condition;
		};

		const auto state = std::make_shared<SharedState>();
		state->chunkCount = (count + grainSize - 1) / grainSize;
		state->grain = grainSize;
		state->count = count;
		state->body = &body;

		const auto processChunks = [](SharedState& s)
		{
			for (;;)
			{
				const size_t chunk = s.nextChunk.fetch_add(1, std::memory_order_relaxed);
				if (chunk >= s.chunkCount)
				{
					return;
				}

				const size_t begin = chunk * s.grain;
				const size_t end = std::min(s.count, begin + s.grain);
				(*s.body)(begin, end);

				if (s.chunksDone.fetch_add(1, std::memory_order_acq_rel) + 1 == s.chunkCount)
				{
					std::scoped_lock lock{ s.mutex };
					s.condition.notify_all();
				}
			}
		};

		// One helper per worker, but never more than there are chunks beyond the caller's first.
		const size_t helperCount = std::min(m_workers.size(), state->chunkCount - 1);
		{
			std::scoped_lock lock{ m_queueMutex };
			for (size_t i = 0; i < helperCount; ++i)
			{
				m_jobs.push_back([state, processChunks]() { processChunks(*state); });
			}
		}
		m_queueCondition.notify_all();

		// The calling thread participates: even if every worker is busy elsewhere, all chunks
		// get processed right here and the wait below can never deadlock.
		processChunks(*state);

		std::unique_lock lock{ state->mutex };
		state->condition.wait(lock, [&state]()
		{
			return state->chunksDone.load(std::memory_order_acquire) == state->chunkCount;
		});
	}

	void TaskSystem::WorkerRun(const size_t workerIndex)
	{
		const std::string name = "mmo_worker_" + std::to_string(workerIndex);
		SetCurrentOsThreadName(name);
		Profiler::GetInstance().SetCurrentThreadName(name);

		for (;;)
		{
			std::function<void()> job;

			{
				std::unique_lock lock{ m_queueMutex };
				m_queueCondition.wait(lock, [this]() { return m_stopping || !m_jobs.empty(); });

				if (m_jobs.empty())
				{
					// Stopping and the queue is drained.
					return;
				}

				job = std::move(m_jobs.front());
				m_jobs.pop_front();
			}

			job();
		}
	}
}
