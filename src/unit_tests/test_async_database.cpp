// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Tests that AsyncDatabaseT carries an ordering key through to the pool, and that unkeyed call
// sites keep the behaviour they had when there was a single connection.

#include "catch.hpp"

#include "base/async_database.h"
#include "base/database_pool.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace mmo;

namespace
{
	/// A database interface small enough to assert on.
	struct ICounterDatabase
	{
		virtual ~ICounterDatabase() = default;
		virtual void Store(uint64 id, int value) = 0;
		virtual int Fetch(uint64 id) = 0;
	};

	struct CounterDatabase final : ICounterDatabase
	{
		std::mutex mutex;
		std::vector<std::string> calls;

		void Store(uint64 id, int value) override
		{
			const std::lock_guard lock(mutex);
			calls.push_back("store:" + std::to_string(id) + ":" + std::to_string(value));
		}

		int Fetch(uint64 id) override
		{
			const std::lock_guard lock(mutex);
			calls.push_back("fetch:" + std::to_string(id));
			return 42;
		}
	};

	/// Captures the key each request was dispatched with, and runs the work inline.
	struct RecordingDispatcher
	{
		CounterDatabase database;
		std::vector<uint64> keys;
		std::vector<std::function<void()>> results;

		AsyncDatabaseT<ICounterDatabase> Make()
		{
			return AsyncDatabaseT<ICounterDatabase>(
				[this](uint64 key, std::function<void(ICounterDatabase&)> work)
				{
					keys.push_back(key);
					work(database);
				},
				[this](std::function<void()> result) { results.push_back(std::move(result)); });
		}
	};
}

// An unkeyed call must land on the global key, which is slot 0 -- exactly where every call went
// when there was one connection. This is what makes migrating call sites incremental rather than
// a flag day.
TEST_CASE("AsyncDatabaseUnkeyedRequestsUseTheGlobalKey", "[async_database]")
{
	RecordingDispatcher dispatcher;
	auto async = dispatcher.Make();

	async.asyncRequest([](int) {}, &ICounterDatabase::Fetch, static_cast<uint64>(7));

	REQUIRE(dispatcher.keys.size() == 1);
	CHECK(dispatcher.keys[0] == database_key::Global);
	REQUIRE(dispatcher.database.calls.size() == 1);
	CHECK(dispatcher.database.calls[0] == "fetch:7");
}

// A keyed call must pass its key through untouched, so the pool can route on it.
TEST_CASE("AsyncDatabaseKeyedRequestsCarryTheKey", "[async_database]")
{
	RecordingDispatcher dispatcher;
	auto async = dispatcher.Make();

	async.asyncRequestKeyed(9001, [](int) {}, &ICounterDatabase::Fetch, static_cast<uint64>(7));

	REQUIRE(dispatcher.keys.size() == 1);
	CHECK(dispatcher.keys[0] == 9001);
}

// The result handler must be posted to the result dispatcher rather than run inline, because it
// touches session state that belongs to the io thread.
TEST_CASE("AsyncDatabaseSendsResultsThroughTheResultDispatcher", "[async_database]")
{
	RecordingDispatcher dispatcher;
	auto async = dispatcher.Make();

	int observed = 0;
	async.asyncRequestKeyed(1, [&observed](int value) { observed = value; },
		&ICounterDatabase::Fetch, static_cast<uint64>(7));

	// Not yet: the handler is queued, not executed.
	CHECK(observed == 0);
	REQUIRE(dispatcher.results.size() == 1);

	dispatcher.results[0]();
	CHECK(observed == 42);
}

// The write-then-read pair from Player::SwitchActionBarClass, end to end through a real pool.
// Both use the character id as their key, so the read must observe the write -- and must land on
// the same connection instance, which is what makes the write visible at all.
TEST_CASE("AsyncDatabaseKeyedWriteThenReadKeepsOrder", "[async_database]")
{
	auto pool = DatabasePool<CounterDatabase>::Create(4,
		[](std::size_t) { return std::make_unique<CounterDatabase>(); });
	REQUIRE(pool != nullptr);

	const uint64 characterId = 555;

	pool->Dispatch(characterId, [characterId](CounterDatabase& database)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(120));
		database.Store(characterId, 1);
	});

	pool->Dispatch(characterId, [characterId](CounterDatabase& database)
	{
		database.Fetch(characterId);
	});

	// Captured before Stop() destroys the slots.
	std::vector<std::string> observed;
	pool->Dispatch(characterId, [&observed](CounterDatabase& database)
	{
		const std::lock_guard lock(database.mutex);
		observed = database.calls;
	});

	pool->Stop();

	REQUIRE(observed.size() == 2);
	CHECK(observed[0] == "store:555:1");
	CHECK(observed[1] == "fetch:555");
}
