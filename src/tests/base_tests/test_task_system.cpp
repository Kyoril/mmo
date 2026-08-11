#include "catch.hpp"
#include "base/task_system.h"
#include "base/typedefs.h"

#include <atomic>
#include <numeric>
#include <vector>

using namespace mmo;

namespace
{
	/// RAII guard so a failing test can't leave the global pool running for other tests.
	struct TaskSystemGuard
	{
		explicit TaskSystemGuard(const size_t threadCount)
		{
			TaskSystem::Get().Initialize(threadCount);
		}

		~TaskSystemGuard()
		{
			TaskSystem::Get().Shutdown();
		}
	};
}

TEST_CASE("ParallelFor degrades to serial when uninitialized", "[task_system]")
{
	REQUIRE_FALSE(TaskSystem::Get().IsInitialized());

	std::vector<int> data(100, 0);
	TaskSystem::Get().ParallelFor(data.size(), 16, [&data](const size_t begin, const size_t end)
	{
		for (size_t i = begin; i < end; ++i)
		{
			data[i] = static_cast<int>(i);
		}
	});

	for (size_t i = 0; i < data.size(); ++i)
	{
		REQUIRE(data[i] == static_cast<int>(i));
	}

	// Uninitialized system reports every thread as the main thread (inert asserts).
	REQUIRE(TaskSystem::Get().IsMainThread());
}

TEST_CASE("ParallelFor covers the whole range exactly once", "[task_system]")
{
	TaskSystemGuard guard{ 4 };
	REQUIRE(TaskSystem::Get().GetWorkerCount() == 4);

	constexpr size_t count = 100000;
	std::vector<std::atomic<int>> touches(count);

	TaskSystem::Get().ParallelFor(count, 64, [&touches](const size_t begin, const size_t end)
	{
		for (size_t i = begin; i < end; ++i)
		{
			touches[i].fetch_add(1, std::memory_order_relaxed);
		}
	});

	for (size_t i = 0; i < count; ++i)
	{
		REQUIRE(touches[i].load() == 1);
	}
}

TEST_CASE("ParallelFor sum reduction matches serial result", "[task_system]")
{
	TaskSystemGuard guard{ 4 };

	constexpr size_t count = 250000;
	std::vector<uint64> values(count);
	std::iota(values.begin(), values.end(), 0);

	std::atomic<uint64> parallelSum{ 0 };
	TaskSystem::Get().ParallelFor(count, 1000, [&values, &parallelSum](const size_t begin, const size_t end)
	{
		uint64 local = 0;
		for (size_t i = begin; i < end; ++i)
		{
			local += values[i];
		}
		parallelSum.fetch_add(local, std::memory_order_relaxed);
	});

	const uint64 serialSum = std::accumulate(values.begin(), values.end(), static_cast<uint64>(0));
	REQUIRE(parallelSum.load() == serialSum);
}

TEST_CASE("ParallelFor runs serially below the grain size", "[task_system]")
{
	TaskSystemGuard guard{ 2 };

	std::atomic<int> calls{ 0 };
	bool ranOnCaller = false;
	TaskSystem::Get().ParallelFor(8, 16, [&](const size_t begin, const size_t end)
	{
		calls.fetch_add(1);
		ranOnCaller = TaskSystem::Get().IsMainThread();
		REQUIRE(begin == 0);
		REQUIRE(end == 8);
	});

	REQUIRE(calls.load() == 1);
	REQUIRE(ranOnCaller);
}

TEST_CASE("ParallelFor with zero count does nothing", "[task_system]")
{
	TaskSystemGuard guard{ 2 };

	std::atomic<int> calls{ 0 };
	TaskSystem::Get().ParallelFor(0, 1, [&calls](size_t, size_t)
	{
		calls.fetch_add(1);
	});

	REQUIRE(calls.load() == 0);
}

TEST_CASE("Dispatch executes queued jobs before shutdown completes", "[task_system]")
{
	std::atomic<int> executed{ 0 };
	constexpr int jobCount = 64;

	{
		TaskSystemGuard guard{ 2 };
		for (int i = 0; i < jobCount; ++i)
		{
			TaskSystem::Get().Dispatch([&executed]()
			{
				executed.fetch_add(1, std::memory_order_relaxed);
			});
		}
		// Shutdown (via guard destructor) must drain the queue.
	}

	REQUIRE(executed.load() == jobCount);
}

TEST_CASE("TaskLatch waits for all completions", "[task_system]")
{
	TaskSystemGuard guard{ 3 };

	constexpr size_t batch = 32;
	TaskLatch latch{ batch };
	std::atomic<size_t> done{ 0 };

	for (size_t i = 0; i < batch; ++i)
	{
		TaskSystem::Get().Dispatch([&latch, &done]()
		{
			done.fetch_add(1, std::memory_order_relaxed);
			latch.CountDown();
		});
	}

	latch.Wait();
	REQUIRE(done.load() == batch);
}

TEST_CASE("IsMainThread distinguishes workers from the initializing thread", "[task_system]")
{
	TaskSystemGuard guard{ 2 };

	REQUIRE(TaskSystem::Get().IsMainThread());

	std::atomic<bool> workerSeesMain{ true };
	TaskLatch latch{ 1 };
	TaskSystem::Get().Dispatch([&workerSeesMain, &latch]()
	{
		workerSeesMain.store(TaskSystem::Get().IsMainThread());
		latch.CountDown();
	});
	latch.Wait();

	REQUIRE_FALSE(workerSeesMain.load());
}

TEST_CASE("ParallelFor stress: repeated small batches stay correct", "[task_system]")
{
	TaskSystemGuard guard{ 4 };

	for (int round = 0; round < 200; ++round)
	{
		constexpr size_t count = 512;
		std::atomic<uint64> sum{ 0 };
		TaskSystem::Get().ParallelFor(count, 8, [&sum](const size_t begin, const size_t end)
		{
			uint64 local = 0;
			for (size_t i = begin; i < end; ++i)
			{
				local += i;
			}
			sum.fetch_add(local, std::memory_order_relaxed);
		});

		REQUIRE(sum.load() == static_cast<uint64>(count) * (count - 1) / 2);
	}
}
