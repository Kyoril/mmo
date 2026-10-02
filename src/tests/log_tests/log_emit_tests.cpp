// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "log/log.h"
#include "log/default_log_levels.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

using namespace mmo;

TEST_CASE("Threads of a process without a TaskSystem emit one at a time", "[log]")
{
	// Without a TaskSystem every thread counts as the main thread and emits directly, as
	// nav_builder's workers and the servers' database thread do. Emission used to be unguarded
	// there, and two threads emitting at once corrupted the heap through the signal's
	// non-atomic reference counts.
	Log log;

	// Deliberately unsynchronized: the slot relies on the log never running it twice at once.
	int64 entriesSeen = 0;
	int32 slotsRunning = 0;
	int32 mostSlotsRunning = 0;
	const scoped_connection connection{ log.signal().connect([&](const LogEntry&)
	{
		++slotsRunning;
		mostSlotsRunning = std::max(mostSlotsRunning, slotsRunning);
		++entriesSeen;
		--slotsRunning;
	}) };

	constexpr int32 threadCount = 8;
	constexpr int32 entriesPerThread = 5000;

	std::vector<std::thread> threads;
	threads.reserve(threadCount);
	for (int32 t = 0; t < threadCount; ++t)
	{
		threads.emplace_back([&log]()
		{
			for (int32 i = 0; i < entriesPerThread; ++i)
			{
				log.Emit(LogEntry(DebugLevel, "entry", std::chrono::system_clock::now()));
			}
		});
	}

	for (auto& thread : threads)
	{
		thread.join();
	}

	CHECK(entriesSeen == static_cast<int64>(threadCount) * entriesPerThread);
	CHECK(mostSlotsRunning == 1);
}

TEST_CASE("A slot may log while it is being emitted to", "[log]")
{
	Log log;

	int32 entriesSeen = 0;
	const scoped_connection connection{ log.signal().connect([&](const LogEntry& entry)
	{
		++entriesSeen;
		if (entry.message == "outer")
		{
			log.Emit(LogEntry(DebugLevel, "inner", std::chrono::system_clock::now()));
		}
	}) };

	log.Emit(LogEntry(DebugLevel, "outer", std::chrono::system_clock::now()));

	CHECK(entriesSeen == 2);
}
