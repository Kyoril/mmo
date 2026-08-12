// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Tests for DatabasePool's routing and ordering guarantees.
//
// The ordering guarantee is the whole reason this class exists rather than a shared
// idle-connection pool: call sites in the realm server queue a write and then a read of the
// same row and rely on them running in order. A pool that hands any work to any free
// connection breaks that silently, so these tests deliberately make the first item slow.

#include "catch.hpp"

#include "base/database_pool.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace mmo;

namespace
{
	/// Stands in for a MySQLDatabase. Records which slot ran what, in order.
	struct FakeDatabase
	{
		explicit FakeDatabase(std::size_t slotIndex) : index(slotIndex) {}

		std::size_t index;
	};

	struct Recorder
	{
		std::mutex mutex;
		std::vector<std::string> events;

		void Record(const std::string& event)
		{
			const std::lock_guard lock(mutex);
			events.push_back(event);
		}
	};

	std::unique_ptr<DatabasePool<FakeDatabase>> MakePool(std::size_t size)
	{
		return DatabasePool<FakeDatabase>::Create(size,
			[](std::size_t index) { return std::make_unique<FakeDatabase>(index); });
	}
}

// Two operations sharing a key must run in the order they were queued, even when the first is
// slow. This is the invariant that lets SetCharacterActionButtons be followed by
// GetActionButtons without the read overtaking the write.
TEST_CASE("DatabasePoolPreservesOrderPerKey", "[database_pool]")
{
	auto pool = MakePool(4);
	REQUIRE(pool != nullptr);

	Recorder recorder;
	const uint64 key = 12345;

	pool->Dispatch(key, [&recorder](FakeDatabase&)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(150));
		recorder.Record("write");
	});

	pool->Dispatch(key, [&recorder](FakeDatabase&) { recorder.Record("read"); });

	pool->Stop();

	REQUIRE(recorder.events.size() == 2);
	CHECK(recorder.events[0] == "write");
	CHECK(recorder.events[1] == "read");
}

// The same key must always reach the same connection, or per-key ordering means nothing.
TEST_CASE("DatabasePoolRoutesAKeyToOneSlot", "[database_pool]")
{
	auto pool = MakePool(4);
	REQUIRE(pool != nullptr);

	std::mutex mutex;
	std::vector<std::size_t> slots;

	for (int repeat = 0; repeat < 20; ++repeat)
	{
		pool->Dispatch(777, [&mutex, &slots](FakeDatabase& database)
		{
			const std::lock_guard lock(mutex);
			slots.push_back(database.index);
		});
	}

	pool->Stop();

	REQUIRE(slots.size() == 20);
	for (const auto slot : slots)
	{
		CHECK(slot == slots[0]);
	}
}

// Different keys must be able to make progress at the same time, otherwise the pool is just a
// slower single connection.
TEST_CASE("DatabasePoolRunsDifferentKeysConcurrently", "[database_pool]")
{
	auto pool = MakePool(4);
	REQUIRE(pool != nullptr);

	std::atomic<int> running{ 0 };
	std::atomic<int> peak{ 0 };

	for (uint64 key = 1; key <= 4; ++key)
	{
		pool->Dispatch(key, [&running, &peak](FakeDatabase&)
		{
			const int now = ++running;
			int previousPeak = peak.load();
			while (now > previousPeak && !peak.compare_exchange_weak(previousPeak, now))
			{
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(120));
			--running;
		});
	}

	pool->Stop();

	// Keys 1..4 hash to distinct slots in a 4-slot pool, so at least two must have overlapped.
	CHECK(peak.load() >= 2);
}

// Stop() must run what is already queued rather than discarding it -- a shutdown that drops
// pending character saves loses player data.
TEST_CASE("DatabasePoolStopDrainsQueuedWork", "[database_pool]")
{
	auto pool = MakePool(2);
	REQUIRE(pool != nullptr);

	std::atomic<int> completed{ 0 };
	for (uint64 key = 0; key < 50; ++key)
	{
		pool->Dispatch(key, [&completed](FakeDatabase&)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
			++completed;
		});
	}

	pool->Stop();

	CHECK(completed.load() == 50);
}

// Stop() is called from the shutdown handler and again by the destructor.
TEST_CASE("DatabasePoolStopIsIdempotent", "[database_pool]")
{
	auto pool = MakePool(2);
	REQUIRE(pool != nullptr);

	pool->Dispatch(1, [](FakeDatabase&) {});
	pool->Stop();
	pool->Stop();

	SUCCEED("second Stop() did not hang or crash");
}

// A pool that cannot open all its connections must fail loudly at startup rather than work
// under light load and fail once traffic reaches the connections that were never opened.
TEST_CASE("DatabasePoolCreateFailsIfAnyConnectionFails", "[database_pool]")
{
	auto pool = DatabasePool<FakeDatabase>::Create(4, [](std::size_t index)
	{
		return index == 2 ? nullptr : std::make_unique<FakeDatabase>(index);
	});

	CHECK(pool == nullptr);
}

// A size of 0 is a configuration mistake, not a request for zero connections.
TEST_CASE("DatabasePoolTreatsZeroSizeAsOne", "[database_pool]")
{
	auto pool = MakePool(0);
	REQUIRE(pool != nullptr);
	CHECK(pool->Size() == 1);
	pool->Stop();
}

// Every connection needs its own keep-alive, on its own thread. A ping issued from anywhere
// else races the queries on that connection, which MySQL reports as the memorable and
// completely misleading "Lost connection to MySQL server during query".
TEST_CASE("DatabasePoolPingsEverySlotOnItsOwnThread", "[database_pool]")
{
	std::mutex mutex;
	std::vector<std::size_t> pinged;
	std::vector<std::thread::id> pingThreads;

	auto pool = DatabasePool<FakeDatabase>::Create(3,
		[](std::size_t index) { return std::make_unique<FakeDatabase>(index); },
		[&mutex, &pinged, &pingThreads](FakeDatabase& database)
		{
			const std::lock_guard lock(mutex);
			pinged.push_back(database.index);
			pingThreads.push_back(std::this_thread::get_id());
		},
		std::chrono::seconds(1));
	REQUIRE(pool != nullptr);

	// Long enough for one interval to elapse on every slot.
	std::this_thread::sleep_for(std::chrono::milliseconds(1400));
	pool->Stop();

	std::vector<std::size_t> seen = pinged;
	std::sort(seen.begin(), seen.end());
	seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
	CHECK(seen.size() == 3);

	// Each ping ran on a distinct thread -- its own slot's.
	std::vector<std::thread::id> threads = pingThreads;
	std::sort(threads.begin(), threads.end());
	threads.erase(std::unique(threads.begin(), threads.end()), threads.end());
	CHECK(threads.size() == 3);
}
