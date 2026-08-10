# Database Connection Pool Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the single MySQL connection and single database thread in the login and realm servers with a pool of connections, without reordering any two database operations that touch the same entity.

**Architecture:** A `DatabasePool<TDatabase>` owning N *slots*, each slot holding one `MySQLDatabase` instance, one `asio::io_service`, and one thread. Work is routed to a slot by an **ordering key** (`key % N`), so two operations on the same character or account always land on the same slot and therefore run in FIFO order — a strictly stronger guarantee than a shared idle-connection pool, and the one this codebase's call sites actually rely on. `AsyncDatabaseT` keeps its existing unkeyed overloads, which route to slot 0, so every current call site compiles and behaves exactly as today until it is given a key.

**Tech Stack:** C++20 (MSVC `/std:c++latest`, gcc `-std=c++2a`), standalone ASIO, libmysql, Catch2.

## Global Constraints

- **Copyright header** on every new or modified source file: `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`
- **Braces:** Allman style — every `{` and `}` on its own line. Always brace `if` bodies.
- **Indentation:** Tabs.
- **Naming:** members `m_camelCase`; methods `PascalCase`; locals and anonymous-namespace free functions `camelCase`; files `snake_case`.
- **Headers:** `#pragma once`; Doxygen comments on all public members.
- **No exceptions** (`SIMPLE_NO_EXCEPTIONS`). Use `ASSERT` / `VERIFY` / `UNREACHABLE` and `DLOG` / `ILOG` / `WLOG` / `ELOG`.
- **Build:** `cmake --build build --config Debug -t <targets> -- /m:4`. The `/m:4` is not optional on the current machine — the pagefile cannot support MSVC's default parallelism.
- **After any change to a shared header, clear `build/src/**/Debug` before rebuilding.** MSBuild's dependency records in this tree have been observed truncated: a stale object linked against a changed class layout produces an access violation in unrelated code. This bit us on 2026-08-10 and cost an hour.
- **Every task ends green:** its own tests pass *and* `powershell -File tools/gate/verify.ps1 -SkipE2E` passes. Full gate at the task boundaries called out below.
- **Do not raise `poolSize` above 1 until Task 7 is complete.** Mixing keyed and unkeyed operations on the same entity reorders them. Task 8 is the only task that raises it.

## Background: the hazard this plan exists to avoid

Today one thread owns one connection, so every database operation runs in the order it was queued. Call sites depend on that. The canonical example is an action-bar class switch in `src/realm_server/player.cpp`:

```cpp
// line 3873 — fire-and-forget write
m_database.asyncRequest([](bool) {}, &IDatabase::SetCharacterActionButtons,
    m_characterData->characterId, m_actionButtonClassId, m_actionButtons);

// line 3901 — read that must see the write above
m_database.asyncRequest(std::move(handler), &IDatabase::GetActionButtons,
    m_characterData->characterId, newClassId);
```

With two connections and no keying, the read can execute before the write and the player's action bar silently reverts. **No E2E scenario covers this today** — `e2e/scenarios/` has nothing exercising action-bar persistence or class switching. The deterministic ordering test in Task 1 and Task 4 is therefore the only thing standing between this change and a data-loss bug, which is why those tests are specified before any wiring.

## What is NOT in scope

- **The world server has no database.** `src/world_server/program.cpp` creates a `dbService`, a work guard and a `dbThread` that nothing ever posts to — there is no `MySQLDatabase` or `AsyncDatabase` anywhere under `src/world_server/`. Task 9 deletes that dead scaffolding; there is no pool to add there.
- Changing any SQL, schema, or `IDatabase` method body.
- Splitting `IDatabase` (63 virtuals on the realm tier) into smaller interfaces. The narrow `AsyncDatabaseT<IGuildDatabase>`-style wrappers already exist and are kept as they are.

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `src/shared/base/database_pool.h` | **New.** `DatabasePool<TDatabase>`: slots, routing, keep-alive, shutdown | Task 1, 2 |
| `src/shared/base/async_database.h` | Async request façade | Dispatch work with an instance + key (Task 4) |
| `src/unit_tests/test_database_pool.cpp` | **New.** Pool routing, ordering, shutdown tests | Task 1, 2 |
| `src/unit_tests/test_async_database.cpp` | **New.** Ordering through the `AsyncDatabaseT` layer | Task 4 |
| `src/login_server/mysql_database.h` / `.cpp` | Login DB | Split `Load()`; drop per-instance ping (Task 2, 3) |
| `src/realm_server/mysql_database.h` / `.cpp` | Realm DB | Split `Load()`; drop per-instance ping (Task 2, 3) |
| `src/login_server/configuration.h` / `.cpp` | Login config | Add `mysqlPoolSize` (Task 5) |
| `src/realm_server/configuration.h` / `.cpp` | Realm config | Add `mysqlPoolSize` (Task 6) |
| `src/login_server/program.cpp` | Login wiring | Pool construction + shutdown (Task 5) |
| `src/realm_server/program.cpp` | Realm wiring | Pool construction for 8 wrappers + shutdown (Task 6) |
| `src/realm_server/player.cpp`, `guild_mgr.cpp`, `friend_mgr.cpp`, `chat_channel_mgr.cpp`, `player_group.cpp`, `motd_manager.cpp` | Realm call sites | Add ordering keys (Task 7) |
| `src/login_server/player.cpp`, `login_http_handlers.cpp` | Login call sites | Add ordering keys (Task 7) |
| `src/world_server/program.cpp` | World wiring | Delete dead `dbService` (Task 9) |
| `docs/testing-servers.md` | Test documentation | Record the ordering invariant (Task 8) |

---

## Task 1: `DatabasePool` with key-ordered slots

**Files:**
- Create: `src/shared/base/database_pool.h`
- Test: `src/unit_tests/test_database_pool.cpp` (create)

**Interfaces:**
- Produces, used by Tasks 2, 4, 5, 6:
  - `namespace mmo::database_key { constexpr uint64 Global = 0; }`
  - `template <class TDatabase> class DatabasePool`
  - `using Work = std::function<void(TDatabase&)>;`
  - `static std::unique_ptr<DatabasePool> Create(std::size_t size, const std::function<std::unique_ptr<TDatabase>(std::size_t index)>& factory);` — returns `nullptr` if the factory returns `nullptr` for any index
  - `void Dispatch(uint64 key, Work work);`
  - `void Stop();`
  - `std::size_t Size() const;`
  - `TDatabase& Primary();` — slot 0's instance, for startup work performed inline before `Dispatch` is ever called

- [ ] **Step 1: Write the failing tests**

Create `src/unit_tests/test_database_pool.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Tests for DatabasePool's routing and ordering guarantees.
//
// The ordering guarantee is the whole reason this class exists rather than a shared
// idle-connection pool: call sites in the realm server queue a write and then a read of the
// same row and rely on them running in order. A pool that hands any work to any free
// connection breaks that silently, so these tests deliberately make the first item slow.

#include "catch.hpp"

#include "base/database_pool.h"

#include <atomic>
#include <chrono>
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

		std::size_t Count()
		{
			const std::lock_guard lock(mutex);
			return events.size();
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
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --config Debug -t unit_tests -- /m:4
```

Expected: compile error, `Cannot open include file: 'base/database_pool.h'`.

- [ ] **Step 3: Implement the pool**

Create `src/shared/base/database_pool.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include "asio/io_service.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace mmo
{
	namespace database_key
	{
		/// Ordering key for work that belongs to no particular entity -- startup loads,
		/// server-wide queries, anything with no character or account to order against.
		///
		/// Global work always lands on slot 0. That is deliberate: it means an unkeyed call
		/// site behaves exactly as it did when there was one connection and one thread.
		constexpr uint64 Global = 0;
	}

	/// A fixed set of database connections, each with its own thread, addressed by an
	/// ordering key.
	///
	/// **Why keys rather than a shared idle list.** The obvious pool hands each piece of work
	/// to whichever connection is free. That maximises throughput and destroys ordering, and
	/// this codebase's call sites rely on ordering: the realm server queues a write and then a
	/// read of the same row and expects the read to see the write (see
	/// Player::SwitchActionBarClass). Routing by key -- same key, same slot, same FIFO queue --
	/// keeps that guarantee while still running unrelated entities in parallel.
	///
	/// The cost is that one slow query blocks other work sharing its key's slot. That is the
	/// right trade here: the alternative is silent, data-losing reordering.
	///
	/// A whole unit of work runs on one connection, so anything that requires connection
	/// affinity within a single database method -- a mysql::Transaction, a
	/// GetLastInsertId() after an INSERT -- is safe by construction.
	template <class TDatabase>
	class DatabasePool final : public NonCopyable
	{
	public:
		/// A unit of database work. Receives the connection it has been routed to.
		using Work = std::function<void(TDatabase&)>;

		/// Creates the instance for one slot. Returning nullptr fails the whole pool.
		using Factory = std::function<std::unique_ptr<TDatabase>(std::size_t index)>;

		/// Opens `size` connections, or returns nullptr if any of them fails.
		///
		/// Failing the whole pool rather than continuing with fewer connections: a half-open
		/// pool works under light load and fails once traffic reaches the connections that
		/// were never opened, which is the worst time to find out.
		///
		/// `size` of 0 is treated as 1.
		///
		/// The factory is called with index 0 first and in order, so a caller can do
		/// one-time work (applying schema migrations) while building slot 0.
		[[nodiscard]] static std::unique_ptr<DatabasePool> Create(std::size_t size, const Factory& factory)
		{
			const std::size_t count = size == 0 ? 1 : size;

			std::unique_ptr<DatabasePool> pool(new DatabasePool());
			pool->m_slots.reserve(count);

			for (std::size_t index = 0; index < count; ++index)
			{
				auto database = factory(index);
				if (!database)
				{
					return nullptr;
				}

				auto slot = std::make_unique<Slot>();
				slot->database = std::move(database);
				slot->work.emplace(slot->service);
				pool->m_slots.push_back(std::move(slot));
			}

			// Threads are started only once every connection is open, so a failed Create()
			// never leaves a thread behind.
			for (auto& slot : pool->m_slots)
			{
				Slot* const raw = slot.get();
				raw->thread = std::thread([raw]() { raw->service.run(); });
			}

			return pool;
		}

		~DatabasePool()
		{
			Stop();
		}

		/// Queues `work` on the connection that `key` maps to. Safe to call from any thread.
		///
		/// Two calls with the same key run in the order they were made. Calls with different
		/// keys may run concurrently and in any order.
		void Dispatch(uint64 key, Work work)
		{
			ASSERT(!m_slots.empty());

			Slot* const slot = m_slots[key % m_slots.size()].get();
			slot->service.post([slot, work = std::move(work)]() { work(*slot->database); });
		}

		/// Runs everything already queued, then closes the connections.
		///
		/// Drains rather than discards: queued work at this point is character saves and
		/// similar, and dropping it loses player data. Safe to call more than once, and from
		/// any thread except a pool thread -- that would join a thread to itself.
		void Stop()
		{
			if (m_stopped)
			{
				return;
			}

			m_stopped = true;

			// Releasing the work guard lets run() return once the queue is empty, rather than
			// stopping the service, which would discard whatever is still queued.
			for (auto& slot : m_slots)
			{
				slot->work.reset();
			}

			for (auto& slot : m_slots)
			{
				if (slot->thread.joinable())
				{
					slot->thread.join();
				}
			}

			m_slots.clear();
		}

		[[nodiscard]] std::size_t Size() const { return m_slots.size(); }

		/// Slot 0's connection, for startup work performed inline before any Dispatch.
		[[nodiscard]] TDatabase& Primary()
		{
			ASSERT(!m_slots.empty());
			return *m_slots[0]->database;
		}

	private:
		DatabasePool() = default;

		/// One connection, one queue, one thread. Held by pointer because io_service is
		/// neither movable nor copyable.
		struct Slot
		{
			std::unique_ptr<TDatabase> database;
			asio::io_service service;
			std::optional<asio::io_service::work> work;
			std::thread thread;
		};

		std::vector<std::unique_ptr<Slot>> m_slots;
		bool m_stopped = false;
	};
}
```

Add `#include "base/macros.h"` for `ASSERT`.

- [ ] **Step 4: Run to verify it passes**

```bash
cmake --build build --config Debug -t unit_tests -- /m:4
```

Then:

```bash
./bin/Debug/unit_tests.exe "[database_pool]"
```

Expected: `All tests passed` across the seven cases.

- [ ] **Step 5: Prove the ordering test would catch a reordering pool**

Temporarily change `Dispatch` to route every call to a *different* slot, which is what a naive idle-list pool does:

```cpp
			static std::atomic<uint64> roundRobin{ 0 };
			Slot* const slot = m_slots[(roundRobin++) % m_slots.size()].get();
```

Run `./bin/Debug/unit_tests.exe "[database_pool]"` and confirm `DatabasePoolPreservesOrderPerKey` and `DatabasePoolRoutesAKeyToOneSlot` **fail**. Then restore the keyed routing and confirm they pass again. A test written after its implementation proves nothing until you have seen it fail.

- [ ] **Step 6: Commit**

```bash
git add src/shared/base/database_pool.h src/unit_tests/test_database_pool.cpp && git commit -m "feat(db): add DatabasePool with per-key ordering guarantees"
```

---

## Task 2: Per-slot keep-alive, replacing the per-database ping

Each `MySQLDatabase` currently owns a keep-alive ping driven by a `TimerQueue` that lives on the single database service. With N connections that service no longer exists, and each connection needs its own ping on its own thread. The pool is the right owner.

**Files:**
- Modify: `src/shared/base/database_pool.h`
- Modify: `src/login_server/mysql_database.h` / `.cpp` — remove `m_pingCountdown`, `m_pingConnection`, `SetNextPingTimer`, and the `TimerQueue&` / `WorkerDispatcher` constructor parameters
- Modify: `src/realm_server/mysql_database.h` / `.cpp` — remove the same, and the `TimerQueue&` constructor parameter
- Test: `src/unit_tests/test_database_pool.cpp`

**Interfaces:**
- Consumes: `DatabasePool<TDatabase>` from Task 1.
- Produces: `DatabasePool::Create(size, factory, keepAlive, keepAliveInterval)` where
  `keepAlive` is `std::function<void(TDatabase&)>` (empty disables it) and `keepAliveInterval`
  is `std::chrono::seconds`. Tasks 5 and 6 pass `[](MySQLDatabase& db) { db.KeepAlive(); }`.
- Produces: `bool MySQLDatabase::KeepAlive()` on both tiers — pings the connection and logs on
  failure. Called only from that connection's own pool thread.

- [ ] **Step 1: Write the failing test**

Append to `src/unit_tests/test_database_pool.cpp`:

```cpp
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
```

Add `#include <algorithm>` to the file's includes.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --config Debug -t unit_tests -- /m:4
```

Expected: compile error — `Create` does not take four arguments.

- [ ] **Step 3: Add keep-alive to the pool**

In `src/shared/base/database_pool.h`, add `#include "asio/steady_timer.hpp"` and `#include <chrono>`, extend `Slot` with a timer, and change `Create`:

```cpp
		/// Called periodically on each slot's own thread to keep its connection from being
		/// dropped by the server's idle timeout. Empty disables keep-alive entirely.
		using KeepAlive = std::function<void(TDatabase&)>;

		[[nodiscard]] static std::unique_ptr<DatabasePool> Create(std::size_t size, const Factory& factory,
			KeepAlive keepAlive = KeepAlive(),
			std::chrono::seconds keepAliveInterval = std::chrono::seconds(30))
```

Inside `Slot`:

```cpp
			std::unique_ptr<asio::steady_timer> pingTimer;
```

After the threads are started in `Create`, arm the timer on each slot when `keepAlive` is set:

```cpp
			if (keepAlive)
			{
				for (auto& slot : pool->m_slots)
				{
					slot->pingTimer = std::make_unique<asio::steady_timer>(slot->service);
					SchedulePing(slot.get(), keepAlive, keepAliveInterval);
				}
			}
```

And add the private helper:

```cpp
		/// Re-arms the keep-alive on the slot's own service, so the ping runs on the same
		/// thread as that connection's queries and cannot race them.
		static void SchedulePing(Slot* slot, KeepAlive keepAlive, std::chrono::seconds interval)
		{
			slot->pingTimer->expires_after(interval);
			slot->pingTimer->async_wait([slot, keepAlive, interval](const asio::error_code& error)
			{
				if (error)
				{
					// Cancelled during shutdown.
					return;
				}

				keepAlive(*slot->database);
				SchedulePing(slot, keepAlive, interval);
			});
		}
```

In `Stop()`, cancel the timers before releasing the work guards, or the pool never drains:

```cpp
			for (auto& slot : m_slots)
			{
				if (slot->pingTimer)
				{
					asio::error_code error;
					slot->pingTimer->cancel(error);
				}
			}
```

- [ ] **Step 4: Run to verify it passes**

```bash
./bin/Debug/unit_tests.exe "[database_pool]"
```

Expected: all eight cases pass.

- [ ] **Step 5: Move `KeepAlive` onto the databases and drop their timers**

In `src/login_server/mysql_database.h`, add to the public section:

```cpp
		/// Pings the connection so the server does not drop it as idle.
		/// Called only from this connection's own pool thread.
		bool KeepAlive();
```

In `src/login_server/mysql_database.cpp`, replace the constructor's ping wiring with the plain method:

```cpp
	bool MySQLDatabase::KeepAlive()
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);
		if (!m_connection.KeepAlive())
		{
			ELOG("MySQL ping failed: " << m_connection.GetErrorMessage());
			return false;
		}

		return true;
	}
```

Delete `m_pingCountdown`, `m_pingConnection`, `SetNextPingTimer()` and their calls, and remove the `TimerQueue&` and `WorkerDispatcher` constructor parameters. The constructor becomes:

```cpp
	MySQLDatabase::MySQLDatabase(mysql::DatabaseInfo connectionInfo)
		: m_connectionInfo(std::move(connectionInfo))
	{
	}
```

Apply the same change to `src/realm_server/mysql_database.h` / `.cpp`, whose constructor becomes:

```cpp
	MySQLDatabase::MySQLDatabase(mysql::DatabaseInfo connectionInfo, const proto::Project& project)
		: m_project(project)
		, m_connectionInfo(std::move(connectionInfo))
	{
	}
```

The call sites in `program.cpp` are updated in Tasks 5 and 6; until then those files will not compile, which is expected — finish this task by building only the shared target:

```bash
cmake --build build --config Debug -t unit_tests -- /m:4
```

- [ ] **Step 6: Commit**

```bash
git add src/shared/base/database_pool.h src/unit_tests/test_database_pool.cpp src/login_server/mysql_database.h src/login_server/mysql_database.cpp src/realm_server/mysql_database.h src/realm_server/mysql_database.cpp && git commit -m "feat(db): move keep-alive into the pool, one ping per connection"
```

---

## Task 3: Apply schema migrations exactly once

`MySQLDatabase::Load()` connects *and* applies every pending `.sql` file. With N instances that would run N times concurrently — a race that at best fails the duplicate-key check and at worst half-applies a migration.

**Files:**
- Modify: `src/login_server/mysql_database.h` / `.cpp`
- Modify: `src/realm_server/mysql_database.h` / `.cpp`

**Interfaces:**
- Produces on both tiers:
  - `bool MySQLDatabase::Connect();` — opens the connection only
  - `bool MySQLDatabase::ApplyMigrations();` — applies pending updates; call on exactly one instance
  - `bool MySQLDatabase::Load();` — retained, equals `Connect() && ApplyMigrations()`, so existing tests and tools keep working

- [ ] **Step 1: Split the method on the login tier**

In `src/login_server/mysql_database.cpp`, `Load()` currently connects and then walks `m_connectionInfo.updatePath` applying `.sql` files. Split it:

```cpp
	bool MySQLDatabase::Connect()
	{
		if (!m_connection.Connect(m_connectionInfo, true))
		{
			ELOG("Could not connect to the login database");
			ELOG(m_connection.GetErrorMessage());
			return false;
		}

		ILOG("Connected to MySQL at " << m_connectionInfo.host << ":" << m_connectionInfo.port);
		return true;
	}

	bool MySQLDatabase::Load()
	{
		// Retained so single-connection callers (tools, tests) keep one entry point.
		return Connect() && ApplyMigrations();
	}
```

Move the update-scanning body of the old `Load()` verbatim into:

```cpp
	bool MySQLDatabase::ApplyMigrations()
	{
		ILOG("Checking for database updates...");
		// ... existing update-scanning body, unchanged ...
	}
```

Declare `Connect()` and `ApplyMigrations()` next to `Load()` in the header with Doxygen comments, noting that `ApplyMigrations` must run on exactly one connection.

- [ ] **Step 2: Repeat on the realm tier**

Apply the identical split in `src/realm_server/mysql_database.h` / `.cpp`.

- [ ] **Step 3: Build**

```bash
cmake --build build --config Debug -t unit_tests -- /m:4
```

Expected: compiles. The servers are wired in Tasks 5 and 6.

- [ ] **Step 4: Commit**

```bash
git add src/login_server/mysql_database.h src/login_server/mysql_database.cpp src/realm_server/mysql_database.h src/realm_server/mysql_database.cpp && git commit -m "refactor(db): separate connecting from applying migrations"
```

---

## Task 4: Route `AsyncDatabaseT` through the pool

**Files:**
- Modify: `src/shared/base/async_database.h`
- Test: `src/unit_tests/test_async_database.cpp` (create)

**Interfaces:**
- Consumes: `DatabasePool`, `database_key::Global` from Task 1.
- Produces:
  - `using WorkDispatcher = std::function<void(uint64 key, std::function<void(TDatabase&)>)>;`
  - `using ResultDispatcher = std::function<void(std::function<void()>)>;`
  - `AsyncDatabaseT(WorkDispatcher asyncWorker, ResultDispatcher resultDispatcher)`
  - Every existing `asyncRequest(...)` overload, unchanged in signature, routing to
    `database_key::Global`
  - A keyed overload of each: `asyncRequestKeyed(uint64 key, ...)` with otherwise identical
    parameters. Task 7 migrates call sites onto these.

**Why a separate name rather than an overload:** an overloaded first parameter of `uint64` is ambiguous against call sites whose first argument is a lambda or a member-function pointer, and worse, a call site that *forgets* the key still compiles. A distinct name makes the keyed form a deliberate act and makes the unmigrated sites greppable.

- [ ] **Step 1: Write the failing test**

Create `src/unit_tests/test_async_database.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Tests that AsyncDatabaseT carries an ordering key through to the pool, and that unkeyed
// call sites keep the behaviour they had when there was a single connection.

#include "catch.hpp"

#include "base/async_database.h"
#include "base/database_pool.h"

#include <string>
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
		std::vector<std::string> calls;

		void Store(uint64 id, int value) override
		{
			calls.push_back("store:" + std::to_string(id) + ":" + std::to_string(value));
		}

		int Fetch(uint64 id) override
		{
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

// An unkeyed call must land on the global key, which is slot 0 -- exactly where every call
// went when there was one connection. This is what makes the migration in Task 7 safe to do
// incrementally.
TEST_CASE("AsyncDatabaseUnkeyedRequestsUseTheGlobalKey", "[async_database]")
{
	RecordingDispatcher dispatcher;
	auto async = dispatcher.Make();

	async.asyncRequest([](int) {}, &ICounterDatabase::Fetch, static_cast<uint64>(7));

	REQUIRE(dispatcher.keys.size() == 1);
	CHECK(dispatcher.keys[0] == database_key::Global);
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

// The write-then-read pair from Player::SwitchActionBarClass, end to end through a real pool.
// Both use the character id as their key, so the read must observe the write.
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

	pool->Stop();

	// Both ran on the same slot, so that slot's instance saw them in order.
	SUCCEED("write and read shared a slot; ordering asserted in DatabasePoolPreservesOrderPerKey");
}
```

Add `#include <thread>` and `#include <chrono>`.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --config Debug -t unit_tests -- /m:4
```

Expected: compile errors — `AsyncDatabaseT` has no two-argument constructor and no `asyncRequestKeyed`.

- [ ] **Step 3: Rewrite `AsyncDatabaseT`**

Replace the class body in `src/shared/base/async_database.h` (keep `detail::RequestProcessor` exactly as it is):

```cpp
	/// Helper class for async database operations.
	///
	/// Requests are handed to a dispatcher along with an ordering key rather than being bound
	/// to one database instance, because the instance is chosen by the pool from that key.
	/// See DatabasePool for why ordering is keyed rather than free.
	template <class TDatabase>
	class AsyncDatabaseT
	{
	public:
		/// Queues work onto the connection that `key` maps to.
		using WorkDispatcher = std::function<void(uint64 key, std::function<void(TDatabase&)>)>;

		/// Queues a result callback back onto the io thread.
		using ResultDispatcher = std::function<void(std::function<void()>)>;

		explicit AsyncDatabaseT(WorkDispatcher asyncWorker, ResultDispatcher resultDispatcher)
			: m_asyncWorker(std::move(asyncWorker))
			, m_resultDispatcher(std::move(resultDispatcher))
		{
		}

	public:
		/// Fire-and-forget call with one argument, on the global ordering key.
		template <class TBase, class A0, class B0_>
		void asyncRequest(void(TBase::*method)(A0), B0_&& b0)
		{
			asyncRequestKeyed(database_key::Global, method, std::forward<B0_>(b0));
		}

		/// Fire-and-forget call with one argument, ordered against `key`.
		template <class TBase, class A0, class B0_>
		void asyncRequestKeyed(uint64 key, void(TBase::*method)(A0), B0_&& b0)
		{
			m_asyncWorker(key, [method, argument = std::forward<B0_>(b0)](TDatabase& database)
			{
				try
				{
					(static_cast<TBase&>(database).*method)(argument);
				}
				catch (const std::exception& ex)
				{
					defaultLogException(ex);
				}
			});
		}

		/// Calls a returning member function; invokes handler on the io thread. Global key.
		template <class ResultHandler, class TBase, class Result, class... A0, class... Args>
		void asyncRequest(ResultHandler&& handler, Result(TBase::*method)(A0...), Args&&... args)
		{
			asyncRequestKeyed(database_key::Global, std::forward<ResultHandler>(handler), method,
				std::forward<Args>(args)...);
		}

		/// Calls a returning member function, ordered against `key`.
		template <class ResultHandler, class TBase, class Result, class... A0, class... Args>
		void asyncRequestKeyed(uint64 key, ResultHandler handler, Result(TBase::*method)(A0...), Args... args)
		{
			auto resultDispatcher = m_resultDispatcher;
			m_asyncWorker(key, [handler, resultDispatcher, method, args...](TDatabase& database)
			{
				detail::RequestProcessor<Result> processor;
				processor(resultDispatcher,
					[&database, method, &args...]() { return (static_cast<TBase&>(database).*method)(args...); },
					handler);
			});
		}

		/// Calls an arbitrary callable against the database. Global key.
		template <class Result, class ResultHandler, class RequestFunction>
		void asyncRequest(RequestFunction&& request, ResultHandler&& handler)
		{
			asyncRequestKeyed<Result>(database_key::Global, std::forward<RequestFunction>(request),
				std::forward<ResultHandler>(handler));
		}

		/// Calls an arbitrary callable against the database, ordered against `key`.
		template <class Result, class ResultHandler, class RequestFunction>
		void asyncRequestKeyed(uint64 key, RequestFunction request, ResultHandler handler)
		{
			auto resultDispatcher = m_resultDispatcher;
			m_asyncWorker(key, [request, handler, resultDispatcher](TDatabase& database)
			{
				detail::RequestProcessor<Result> processor;
				processor(resultDispatcher, [&database, request]() { return request(&database); }, handler);
			});
		}

		[[nodiscard]] const WorkDispatcher& GetAsyncWorker() const { return m_asyncWorker; }
		[[nodiscard]] const ResultDispatcher& GetResultDispatcher() const { return m_resultDispatcher; }

	private:
		const WorkDispatcher m_asyncWorker;
		const ResultDispatcher m_resultDispatcher;
	};
```

Add `#include "base/database_pool.h"` (for `database_key::Global`) and `#include "base/typedefs.h"`.

**Note the removed member:** `GetDatabase()` is gone, because there is no single database any more. Build errors from it are call sites that must be reworked to go through a request; there were none outside `program.cpp` at the time of writing — verify with `grep -rn "GetDatabase()" src/`.

- [ ] **Step 4: Run to verify it passes**

```bash
cmake --build build --config Debug -t unit_tests -- /m:4 && ./bin/Debug/unit_tests.exe "[async_database],[database_pool]"
```

Expected: `All tests passed`.

- [ ] **Step 5: Commit**

```bash
git add src/shared/base/async_database.h src/unit_tests/test_async_database.cpp && git commit -m "feat(db): dispatch async requests by ordering key"
```

---

## Task 5: Wire the login server to the pool

**Files:**
- Modify: `src/login_server/configuration.h` / `.cpp`
- Modify: `src/login_server/program.cpp`
- Modify: `tools/e2e/e2e_up.ps1` where it writes the login config

No server `.cfg` files are checked in (`config/` is empty and there is no `*_server.cfg` outside `e2e/runtime/`), so the config default in code and the E2E generator are the only two places the value appears.

**Interfaces:**
- Consumes: `DatabasePool`, `AsyncDatabaseT`'s new constructor, `Connect()` / `ApplyMigrations()`.
- Produces: `Configuration::mysqlPoolSize` (`size_t`, default `1`).

- [ ] **Step 1: Add the config key**

In `src/login_server/configuration.h`, after `mysqlUpdatePath`:

```cpp
		/// Number of database connections to open.
		///
		/// Defaults to 1, which reproduces the single-connection behaviour exactly. Raise it
		/// only once every call site that touches a given entity supplies an ordering key --
		/// see docs/superpowers/plans/2026-08-10-database-connection-pool.md.
		size_t mysqlPoolSize;
```

In `src/login_server/configuration.cpp`, add `, mysqlPoolSize(1)` to the initialiser list and inside the `mysqlDatabase` table block:

```cpp
				mysqlPoolSize = static_cast<size_t>(mysqlDatabaseTable->getInteger("poolSize", static_cast<int>(mysqlPoolSize)));
```

Add the same key to the `save`/`addKey` section alongside `updatePath`, following the surrounding style.

- [ ] **Step 2: Replace the single database with the pool**

In `src/login_server/program.cpp`, delete the `dbService`, `dbWork`, and `dbThread` declarations and their `join()`, and replace the database setup block with:

```cpp
		auto databasePool = DatabasePool<MySQLDatabase>::Create(config.mysqlPoolSize,
			[&config](std::size_t index) -> std::unique_ptr<MySQLDatabase>
			{
				auto database = std::make_unique<MySQLDatabase>(mysql::DatabaseInfo{
					config.mysqlHost, config.mysqlPort, config.mysqlUser,
					config.mysqlPassword, config.mysqlDatabase, config.mysqlUpdatePath });

				if (!database->Connect())
				{
					return nullptr;
				}

				// Only the first connection migrates. Running the update scripts from several
				// connections at once races on the history table.
				if (index == 0 && !database->ApplyMigrations())
				{
					return nullptr;
				}

				return database;
			},
			[](MySQLDatabase& database) { database.KeepAlive(); });

		if (!databasePool)
		{
			ELOG("Could not open the login database");
			return 1;
		}

		ILOG("Database ready with " << databasePool->Size() << " connection(s)");

		const auto sync = [&ioService](std::function<void()> action) { ioService.post(std::move(action)); };
		const auto async = [&databasePool](uint64 key, std::function<void(IDatabase&)> work)
		{
			databasePool->Dispatch(key, [work = std::move(work)](MySQLDatabase& database) { work(database); });
		};

		AsyncDatabase asyncDatabase{ async, sync };
```

Add `#include "base/database_pool.h"`.

The `WebService` constructor takes `IDatabase&` and calls it **synchronously from HTTP handlers on an io thread**, while pool threads use the same instance. That is safe for the same reason it is safe today: every `MySQLDatabase` method takes `m_databaseMutex`, so the io thread and the owning pool thread serialise against each other. Slot 0 is therefore no worse off than the single connection was. It does mean a slow REST query blocks slot 0's queue, which is another reason entity-scoped work should be keyed away from `Global`.

Pass `databasePool->Primary()`:

```cpp
		auto webService = std::make_unique<WebService>(
			ioService, config.webPort, config.webPassword, playerManager, realmManager,
			databasePool->Primary());
```

- [ ] **Step 3: Wire the pool into shutdown**

In the shutdown handler added by the graceful-shutdown work, replace `dbWork.reset();` with:

```cpp
			// Stopped after the io threads are done, so nothing can still be queueing database
			// work. The pool drains rather than discards: queued character and account writes
			// at this point are player data.
			if (databasePool)
			{
				databasePool->Stop();
			}
```

Capture `&databasePool` in the handler's capture list instead of `&dbWork`. After `ioService.run()` returns and the threads are joined, call `databasePool->Stop();` again — it is idempotent and covers the non-signal exit path.

- [ ] **Step 4: Update the E2E config writer**

In `tools/e2e/e2e_up.ps1`, find where it writes the `mysqlDatabase` block of `login_server.cfg` and add `poolSize = 1` alongside `updatePath`, so the E2E stack pins the value explicitly rather than inheriting a default that Task 8 will change.

- [ ] **Step 5: Build and run the gate**

```bash
cmake --build build --config Debug -t login_server login_server_tests unit_tests -- /m:4
```

Then the full gate:

```bash
powershell -File tools/gate/verify.ps1
```

Expected: `Gate result: GREEN`. With `poolSize = 1` this is behaviourally identical to before, so any failure here is a wiring mistake, not an ordering one.

- [ ] **Step 6: Verify clean shutdown still works**

```bash
python tools/shutdown_check.py
```

Expected: `ALL PASS`. The login server now owns pool threads instead of a db thread; if `Stop()` is missing from the handler, the process will hang and this catches it.

- [ ] **Step 7: Commit**

```bash
git add src/login_server tools/e2e/e2e_up.ps1 && git commit -m "feat(login): serve database work from a connection pool"
```

---

## Task 6: Wire the realm server to the pool

The realm server builds eight `AsyncDatabaseT` wrappers over one `MySQLDatabase`. Each needs a dispatcher that adapts the pool's `MySQLDatabase&` to that wrapper's interface.

**Files:**
- Modify: `src/realm_server/configuration.h` / `.cpp`
- Modify: `src/realm_server/program.cpp`
- Modify: `tools/e2e/e2e_up.ps1`

**Interfaces:**
- Consumes: everything from Tasks 1–4.
- Produces: `Configuration::mysqlPoolSize` (`size_t`, default `1`).

- [ ] **Step 1: Add the config key**

Identical to Task 5 Step 1, in `src/realm_server/configuration.h` / `.cpp`: add `size_t mysqlPoolSize;` with the same Doxygen comment, `, mysqlPoolSize(1)` in the initialiser list, and the `poolSize` read inside the `mysqlDatabase` table block.

- [ ] **Step 2: Replace the single database with the pool**

In `src/realm_server/program.cpp`, delete `dbService`, `dbWork`, `dbTimerQueue`, `dbThread` and its `join()`, and replace the database block:

```cpp
		auto databasePool = DatabasePool<MySQLDatabase>::Create(config.mysqlPoolSize,
			[&config, &project](std::size_t index) -> std::unique_ptr<MySQLDatabase>
			{
				auto database = std::make_unique<MySQLDatabase>(mmo::mysql::DatabaseInfo{
					config.mysqlHost, config.mysqlPort, config.mysqlUser,
					config.mysqlPassword, config.mysqlDatabase, config.mysqlUpdatePath }, project);

				if (!database->Connect())
				{
					return nullptr;
				}

				if (index == 0 && !database->ApplyMigrations())
				{
					return nullptr;
				}

				return database;
			},
			[](MySQLDatabase& database) { database.KeepAlive(); });

		if (!databasePool)
		{
			ELOG("Could not open the realm database");
			return 1;
		}

		ILOG("Database ready with " << databasePool->Size() << " connection(s)");
```

- [ ] **Step 3: Build the eight dispatchers**

Still in `src/realm_server/program.cpp`, replacing the old `async` / `sync` lambdas:

```cpp
		const auto sync = [&ioService](std::function<void()> action) { ioService.post(std::move(action)); };

		// One adaptor per narrow interface. MySQLDatabase implements all of them, so each
		// adaptor is just a static upcast performed on the pool thread.
		const auto makeWorker = [&databasePool](auto interfaceTag)
		{
			using TInterface = typename decltype(interfaceTag)::type;
			return [&databasePool](uint64 key, std::function<void(TInterface&)> work)
			{
				databasePool->Dispatch(key, [work = std::move(work)](MySQLDatabase& database) { work(database); });
			};
		};

		template_tag<IDatabase>            fullTag;
		template_tag<IGuildDatabase>       guildTag;
		template_tag<IGroupDatabase>       groupTag;
		template_tag<IFriendDatabase>      friendTag;
		template_tag<IMOTDDatabase>        motdTag;
		template_tag<IChatChannelDatabase> chatChannelTag;

		AsyncDatabase asyncDatabase{ makeWorker(fullTag), sync };
		AsyncGuildDatabase asyncGuildDb{ makeWorker(guildTag), sync };
		AsyncFriendDatabase asyncFriendDb{ makeWorker(friendTag), sync };
		AsyncMOTDDatabase asyncMotdDb{ makeWorker(motdTag), sync };
		AsyncChatChannelDatabase asyncChatChannelDb{ makeWorker(chatChannelTag), sync };
```

Add the tag helper at the top of the anonymous namespace in that file:

```cpp
		/// Carries an interface type into makeWorker, which cannot take an explicit template
		/// argument because it is a lambda.
		template <class T>
		struct template_tag
		{
			using type = T;
		};
```

`AsyncGroupDatabase` is constructed inline where player groups are restored:

```cpp
					auto group = std::make_shared<PlayerGroup>(groupId, playerManager,
						AsyncGroupDatabase{ makeWorker(groupTag), sync }, timerQueue);
```

- [ ] **Step 4: Fix the remaining direct-database uses**

`database->ListGroups()` at startup and the `WebService` construction both used the raw pointer. Replace `*database` with `databasePool->Primary()` and `database->ListGroups()` with `databasePool->Primary().ListGroups()`. Both run before the io service starts, so using slot 0 inline is safe and matches the previous behaviour.

- [ ] **Step 5: Wire the pool into shutdown**

In the realm shutdown handler, replace `dbWork.reset();` with the pool stop, and delete the now-gone `dbTimerQueue.Stop();`:

```cpp
			if (databasePool)
			{
				databasePool->Stop();
			}
```

Capture `&databasePool` instead of `&dbWork` and `&dbTimerQueue`.

- [ ] **Step 6: Build and run the full gate**

```bash
cmake --build build --config Debug -t realm_server realm_server_tests unit_tests -- /m:4
```

```bash
powershell -File tools/gate/verify.ps1
```

Expected: `Gate result: GREEN`, with all 14 E2E scenarios passing. `poolSize` is still 1, so behaviour is unchanged.

- [ ] **Step 7: Verify shutdown**

```bash
python tools/shutdown_check.py
```

Expected: `ALL PASS`.

- [ ] **Step 8: Commit**

```bash
git add src/realm_server tools/e2e/e2e_up.ps1 && git commit -m "feat(realm): serve database work from a connection pool"
```

---

## Task 7: Give every entity-scoped call site an ordering key

This is the task that makes raising `poolSize` safe. It is mechanical but must be **complete per entity type**: if one operation on characters is keyed and another is not, they land on different slots and can reorder — which is worse than today.

**Files:**
- Modify: `src/realm_server/player.cpp` (the bulk of the 68 call sites), `guild_mgr.cpp`, `friend_mgr.cpp`, `chat_channel_mgr.cpp`, `player_group.cpp`, `motd_manager.cpp`
- Modify: `src/login_server/player.cpp`, `src/login_server/login_http_handlers.cpp`

**Interfaces:**
- Consumes: `asyncRequestKeyed` from Task 4.

**The keying rule:**

| Operation touches | Key |
|---|---|
| One character's rows | that character's `characterId` |
| One account's rows | that account's `accountId` |
| One guild's rows | that guild's id |
| One group's rows | that group's id |
| Nothing entity-scoped (startup loads, server-wide stats, MOTD) | `database_key::Global` — leave the call unkeyed |

Character and account ids come from different id spaces and can collide in `key % N`. That is harmless: a collision only means two unrelated entities share a slot, costing a little parallelism, never correctness.

- [ ] **Step 1: Enumerate the call sites**

```bash
grep -rn "asyncRequest(" src/realm_server src/login_server --include=*.cpp
```

Expected: 77 lines (68 realm, 9 login). Work through them file by file; do not skip any, and record the decision for each in the commit message if it is not obvious.

- [ ] **Step 2: Convert the canonical ordering-dependent pair first**

`SetCharacterActionButtons` returns `void` and takes three arguments, so it uses the *returning*
overload with a `[](bool){}` handler — as the original does — and its keyed form is therefore
`asyncRequestKeyed(key, handler, method, args...)`. In `src/realm_server/player.cpp`, the write at
line 3873 becomes:

```cpp
			m_database.asyncRequestKeyed(m_characterData->characterId, [](bool) {},
				&IDatabase::SetCharacterActionButtons,
				m_characterData->characterId, m_actionButtonClassId, m_actionButtons);
```

And the read at line 3901:

```cpp
		m_database.asyncRequestKeyed(m_characterData->characterId, std::move(handler),
			&IDatabase::GetActionButtons, m_characterData->characterId, newClassId);
```

- [ ] **Step 3: Convert the remaining realm call sites**

Apply the keying rule to every remaining `asyncRequest(` in `src/realm_server`. Character-scoped methods are recognisable by taking a character guid or `m_characterData->characterId` as their first argument: `CharacterEnterWorld`, `GetCharacterViewsByAccountId` (key on `m_accountId`), `SetCharacterActionButtons`, `GetActionButtons`, `GetMailList`, `DeleteMail`, `MarkMailRead`, `GetUnreadMailCount`, `AddFriend`, `RemoveFriend`, `GetCharactersWithFriend`, `LoadCharacterChannelStates`, `SetCharacterChannelState`, `ChatMessage`, `CreateCharacter` (key on `m_accountId` — the character does not exist yet).

Leave genuinely global calls unkeyed: `ListGroups`, MOTD reads and writes, and anything server-wide.

- [ ] **Step 4: Convert the login call sites**

In `src/login_server/player.cpp` and `login_http_handlers.cpp`, key on `m_accountId` (or the looked-up `account->id`) for `PlayerLogin`, `PlayerLoginFailed`, `GetActiveAccountFeatures`, and `GetAccountDataByName`. `AddPlayerCountSample` is server-wide and stays unkeyed.

`GetAccountDataByName` runs before `m_accountId` is known. Key it on a hash of the account name so that repeated logins for one account serialise:

```cpp
		m_database.asyncRequestKeyed(std::hash<std::string>{}(m_accountName), std::move(handler),
			&IDatabase::GetAccountDataByName, std::cref(m_accountName));
```

- [ ] **Step 5: Confirm nothing entity-scoped was missed**

```bash
grep -rn "asyncRequest(" src/realm_server src/login_server --include=*.cpp
```

Every remaining hit must be a genuinely global operation. Review the list one by one; a character- or account-scoped call left in this list is precisely the bug this task exists to prevent.

- [ ] **Step 6: Build and run the full gate**

```bash
cmake --build build --config Debug -t login_server realm_server unit_tests login_server_tests realm_server_tests -- /m:4
```

```bash
powershell -File tools/gate/verify.ps1
```

Expected: `Gate result: GREEN`. `poolSize` is still 1, so this proves the keying did not change behaviour.

- [ ] **Step 7: Commit**

```bash
git add src/realm_server src/login_server && git commit -m "refactor(db): key entity-scoped database requests for ordering"
```

---

## Task 8: Raise the pool size and prove it

**Files:**
- Modify: `src/login_server/configuration.cpp`, `src/realm_server/configuration.cpp` — default `4`
- Modify: `tools/e2e/e2e_up.ps1` — `poolSize = 4`
- Modify: `docs/testing-servers.md`

- [ ] **Step 1: Raise the defaults**

Change `, mysqlPoolSize(1)` to `, mysqlPoolSize(4)` in both `configuration.cpp` files, and update both Doxygen comments to say the default is 4.

Set `poolSize = 4` in the config that `tools/e2e/e2e_up.ps1` writes for both tiers.

- [ ] **Step 2: Run the full gate**

```bash
powershell -File tools/gate/verify.ps1
```

Expected: `Gate result: GREEN`, 14/14 E2E. This is the first run with real concurrency; a failure here is an ordering bug, so treat it as one — find which pair of operations raced rather than retrying.

- [ ] **Step 3: Soak with concurrency**

Three suite passes against one long-lived stack, which is what exercises sustained concurrent database traffic:

```bash
powershell -File tools/e2e/e2e_run.ps1 -KeepStack
```

Then twice more:

```bash
powershell -File tools/e2e/e2e_run.ps1 -NoStack
```

Confirm all three are GREEN, that the realm server's working set is flat across passes, and that `e2e/runtime/realm/logs/*.log` contains no `[Error]` lines. Tear down with `powershell -File tools/e2e/e2e_down.ps1`.

- [ ] **Step 4: Verify shutdown drains under load**

```bash
python tools/shutdown_check.py
```

Expected: `ALL PASS`. With four connections the pool has four threads to join; a missing `Stop()` or a still-armed keep-alive timer hangs the process here.

- [ ] **Step 5: Document the invariant**

Add to `docs/testing-servers.md`, under a new `## Database ordering` heading:

```markdown
## Database ordering

Database work is routed to a connection by an **ordering key** (`DatabasePool::Dispatch`).
Two operations sharing a key run in the order they were queued; two with different keys may
run concurrently and in any order.

**When adding a database call, key it on the entity it touches** — character id, account id,
guild id, group id — using `asyncRequestKeyed`. Leave only genuinely server-wide operations
unkeyed. A call left unkeyed while its neighbours are keyed lands on a different connection
and can reorder against them; the failure looks like data that silently reverts, most likely
noticed as a player's action bar resetting after a class switch.

No E2E scenario covers action-bar persistence. The guard is
`DatabasePoolPreservesOrderPerKey` in `src/unit_tests/test_database_pool.cpp`, which makes the
first operation slow on purpose so a reordering pool cannot pass it.
```

- [ ] **Step 6: Commit**

```bash
git add src/login_server/configuration.cpp src/realm_server/configuration.cpp tools/e2e/e2e_up.ps1 docs/testing-servers.md && git commit -m "feat(db): default to four database connections per tier"
```

---

## Task 9: Delete the world server's dead database scaffolding

`src/world_server/program.cpp` creates an `asio::io_service dbService`, a work guard, and a `dbThread` — and nothing ever posts to any of them. There is no `MySQLDatabase` or `AsyncDatabase` anywhere under `src/world_server/`. It is a thread and a work guard that exist only to be shut down.

**Files:**
- Modify: `src/world_server/program.cpp`

- [ ] **Step 1: Confirm it really is unused**

```bash
grep -rn "dbService\|dbWork\|dbThread\|MySQLDatabase\|AsyncDatabase" src/world_server
```

Expected: hits only in `program.cpp`, only the declarations, the `dbThread` creation and join, and the shutdown handler's `dbWork.reset()`. If anything else appears, stop and reassess — this task assumes the world tier has no database.

- [ ] **Step 2: Remove them**

Delete the `dbService`, `dbWork`, `dbTimerQueue` (if present), `dbThread` declarations, the `dbThread.join()`, and the `dbWork.reset()` in the shutdown handler, along with `&dbWork` from its capture list.

- [ ] **Step 3: Build and verify shutdown**

```bash
cmake --build build --config Debug -t world_server -- /m:4
```

```bash
python tools/shutdown_check.py
```

Expected: `ALL PASS`, with the world server still exiting cleanly — it now has one fewer thread to wind down.

- [ ] **Step 4: Run the full gate**

```bash
powershell -File tools/gate/verify.ps1
```

Expected: `Gate result: GREEN`.

- [ ] **Step 5: Commit**

```bash
git add src/world_server/program.cpp && git commit -m "refactor(world): remove the unused database service and thread"
```

---

## Final Verification

- [ ] `powershell -File tools/gate/verify.ps1` is green, and `tools/gate/last_report.json` matches HEAD with `e2e_skipped: false`.
- [ ] `python tools/shutdown_check.py` reports `ALL PASS` for all three tiers.
- [ ] `./bin/Debug/unit_tests.exe "[database_pool],[async_database]"` passes.
- [ ] The reordering experiment from Task 1 Step 5 has been performed and the ordering tests were seen to fail without keyed routing.
- [ ] Three back-to-back E2E passes against one stack at `poolSize = 4`, with flat realm-server memory and no `[Error]` lines.
- [ ] `grep -rn "asyncRequest(" src/realm_server src/login_server --include=*.cpp` — every remaining unkeyed call reviewed and confirmed server-wide.
- [ ] A full client build succeeds, and `build/src/**/Debug` was cleared first (this plan changes `async_database.h`, which the client's realm connector includes).
- [ ] `/gate` then `/ship`.

## Deferred

- **Sizing the pool from load rather than a fixed 4.** Four is a guess that comfortably beats one; tuning needs production numbers.
- **Splitting reads from writes across pools.** Would need read-replica support that does not exist.
- **The login server's missing duplicate-login prevention.** ROSE's `session_registry` returns `ALREADY_LOGGEDIN` and expires handoff tokens; this codebase has neither. Unrelated to pooling, worth its own plan.
