// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/macros.h"
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
		[[nodiscard]] static std::unique_ptr<DatabasePool> Create(std::size_t size, const Factory& factory)
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
		void Dispatch(uint64 key, Work work)
		{
			ASSERT(!m_slots.empty());

			Slot* const slot = m_slots[key % m_slots.size()].get();
			slot->service.post([slot, work = std::move(work)]() { work(*slot->database); });
		}

		/// Runs everything already queued, then closes the connections.
		///
		/// Drains rather than discards: queued work at this point is character saves and similar,
		/// and dropping it loses player data. Safe to call more than once, and from any thread
		/// except a pool thread -- that would join a thread to itself.
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

		/// Number of open connections.
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
			std::thread thread;
		};

		std::vector<std::unique_ptr<Slot>> m_slots;
		bool m_stopped = false;
	};
}
