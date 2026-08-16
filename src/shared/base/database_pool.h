// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/macros.h"
#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "log/default_log_levels.h"

#include "asio/io_service.hpp"
#include "asio/steady_timer.hpp"

#include <atomic>
#include <chrono>
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

	/// A fixed set of database connections, each with its own thread, addressed by an ordering
	/// key.
	///
	/// **Why keys rather than a shared idle list.** The obvious pool hands each piece of work to
	/// whichever connection is free. That maximises throughput and destroys ordering, and this
	/// codebase's call sites rely on ordering: the realm server queues a write and then a read of
	/// the same row and expects the read to see the write (see Player::SwitchActionBarClass).
	/// Routing by key -- same key, same slot, same FIFO queue -- keeps that guarantee while still
	/// running unrelated entities in parallel.
	///
	/// The cost is that one slow query blocks other work sharing its key's slot. That is the right
	/// trade here: the alternative is silent, data-losing reordering.
	///
	/// A whole unit of work runs on one connection, so anything requiring connection affinity
	/// within a single database method -- a mysql::Transaction, a GetLastInsertId() after an
	/// INSERT -- is safe by construction.
	template <class TDatabase>
	class DatabasePool final : public NonCopyable
	{
	public:
		/// A unit of database work. Receives the connection it has been routed to.
		using Work = std::function<void(TDatabase&)>;

		/// Creates the instance for one slot. Returning nullptr fails the whole pool.
		using Factory = std::function<std::unique_ptr<TDatabase>(std::size_t index)>;

		/// Called periodically on each slot's own thread to keep its connection from being
		/// dropped by the server's idle timeout. Empty disables keep-alive entirely.
		///
		/// It must run on the slot's own thread: a ping issued from anywhere else races that
		/// connection's queries, which MySQL reports as "Lost connection to MySQL server during
		/// query" -- a message that sends you looking for a network fault that is not there.
		using KeepAlive = std::function<void(TDatabase&)>;

	public:
		/// Opens `size` connections, or returns nullptr if any of them fails.
		///
		/// Failing the whole pool rather than continuing with fewer connections: a half-open pool
		/// works under light load and fails once traffic reaches the connections that were never
		/// opened, which is the worst time to find out.
		///
		/// `size` of 0 is treated as 1.
		///
		/// The factory is called with index 0 first and in order, so a caller can do one-time work
		/// -- applying schema migrations -- while building slot 0.
		[[nodiscard]] static std::unique_ptr<DatabasePool> Create(std::size_t size, const Factory& factory,
			KeepAlive keepAlive = KeepAlive(),
			std::chrono::seconds keepAliveInterval = std::chrono::seconds(30))
		{
			const std::size_t count = size == 0 ? 1 : size;

			// Not make_unique: the constructor is private, and this factory is the only way in
			// precisely so a pool cannot exist with unopened connections.
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

			// Threads start only once every connection is open, so a failed Create() never leaves
			// a thread behind to be joined by nobody.
			for (auto& slot : pool->m_slots)
			{
				Slot* const raw = slot.get();
				raw->thread = std::thread([raw]() { raw->service.run(); });
			}

			if (keepAlive)
			{
				for (auto& slot : pool->m_slots)
				{
					slot->pingTimer = std::make_unique<asio::steady_timer>(slot->service);
					SchedulePing(slot.get(), keepAlive, keepAliveInterval);
				}
			}

			return pool;
		}

		~DatabasePool()
		{
			Stop();
		}

	public:
		/// Queues `work` on the connection that `key` maps to. Safe to call from any thread.
		///
		/// Two calls with the same key run in the order they were made. Calls with different keys
		/// may run concurrently and in any order.
		///
		/// After Stop() the work is dropped and a warning logged, because there is no longer a
		/// thread that could run it. Callers waiting on a result never get one -- which is the
		/// honest outcome, and the reason the drop is not silent.
		void Dispatch(uint64 key, Work work)
		{
			ASSERT(!m_slots.empty());

			if (m_stopped)
			{
				WLOG("Database work for key " << key << " arrived after the pool was stopped and has been dropped");
				return;
			}

			Slot* const slot = m_slots[key % m_slots.size()].get();
			slot->service.post([slot, work = std::move(work)]() { work(*slot->database); });
		}

		/// Runs everything already queued, then shuts the connections' threads down.
		///
		/// Drains rather than discards: queued work at this point is character saves and similar,
		/// and dropping it loses player data. Safe to call more than once, and from any thread
		/// except a pool thread -- that would join a thread to itself.
		///
		/// The slots themselves are kept until the pool is destroyed rather than released here,
		/// which is what makes a Dispatch racing this harmless. The stopped flag alone would not:
		/// a caller can read it as false and reach the routing arithmetic after this returns, so
		/// releasing the slots would leave that caller dividing by a zero slot count. Keeping them
		/// means the worst case is a post to a service whose run() has already returned -- a
		/// handler that is never executed and is discarded when the service is destroyed. The
		/// connections close at destruction, a scope later; both servers destroy the pool as soon
		/// as their io service returns.
		void Stop()
		{
			// Exchanged rather than checked and then set: the shutdown handler and the destructor
			// both call this, and on the login server they can be on different threads. A plain
			// check lets both past it and into join(), which is undefined on one thread object.
			if (m_stopped.exchange(true))
			{
				return;
			}

			// Cancelled before the work guards are released, or the pool never runs dry: an armed
			// keep-alive timer is outstanding work just like anything else.
			for (auto& slot : m_slots)
			{
				if (slot->pingTimer)
				{
					asio::error_code error;
					slot->pingTimer->cancel(error);
				}
			}

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
		}

		/// Number of connections the pool was opened with. Unchanged by Stop().
		[[nodiscard]] std::size_t Size() const { return m_slots.size(); }

		/// Slot 0's connection, for startup work performed inline before any Dispatch.
		[[nodiscard]] TDatabase& Primary()
		{
			ASSERT(!m_slots.empty());
			return *m_slots[0]->database;
		}

	private:
		DatabasePool() = default;

		/// One connection, one queue, one thread. Held by pointer because io_service is neither
		/// movable nor copyable.
		struct Slot
		{
			std::unique_ptr<TDatabase> database;
			asio::io_service service;
			std::optional<asio::io_service::work> work;
			std::unique_ptr<asio::steady_timer> pingTimer;
			std::thread thread;
		};

		/// Re-arms the keep-alive on the slot's own service, so the ping runs on the same thread
		/// as that connection's queries and cannot race them.
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

		/// Filled by Create() before any thread exists and never modified afterwards -- not even by
		/// Stop() -- so concurrent readers need no synchronisation of their own.
		std::vector<std::unique_ptr<Slot>> m_slots;

		/// Atomic because Dispatch reads it from any thread while Stop writes it from whichever
		/// thread handled the shutdown signal.
		std::atomic<bool> m_stopped{ false };
	};
}
