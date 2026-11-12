// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "parallel_for.h"

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace mmo::pcg
{
	size_t HardwareWorkers()
	{
		return std::max<size_t>(1, std::thread::hardware_concurrency());
	}

	void ParallelFor(const size_t count, const size_t workers, const std::function<void(size_t worker, size_t index)>& body)
	{
		const size_t threads = std::min(workers, count);
		if (threads <= 1)
		{
			for (size_t index = 0; index < count; ++index)
			{
				body(0, index);
			}
			return;
		}

		std::atomic<size_t> next{ 0 };
		const auto work = [&](const size_t worker)
		{
			for (size_t index = next++; index < count; index = next++)
			{
				body(worker, index);
			}
		};
		std::vector<std::thread> started;
		started.reserve(threads - 1);
		for (size_t worker = 1; worker < threads; ++worker)
		{
			started.emplace_back(work, worker);
		}
		work(0);
		for (std::thread& thread : started)
		{
			thread.join();
		}
	}
}
