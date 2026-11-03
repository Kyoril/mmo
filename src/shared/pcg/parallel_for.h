// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <cstddef>
#include <functional>

namespace mmo::pcg
{
	/// @brief Number of worker threads to use for CPU-bound PCG work: one per hardware thread, at least 1.
	size_t HardwareWorkers();

	/// @brief Calls body(worker, index) for every index in [0, count) exactly once, sharing the indices out over up to
	///        `workers` threads that each take the next unclaimed index (the calling thread is worker 0). No worker
	///        number is used on two threads at once, so per-worker state indexed by it needs no lock. Returns when every
	///        index is done.
	/// @details Plain threads started per call (the PCG builds run in the editor and in tools, which do not initialise
	///          the engine's TaskSystem); meant for loops that take milliseconds or more. With workers <= 1 or count <= 1
	///          the loop runs on the calling thread.
	void ParallelFor(size_t count, size_t workers, const std::function<void(size_t worker, size_t index)>& body);
}
