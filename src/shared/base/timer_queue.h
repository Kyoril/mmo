// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "typedefs.h"
#include "non_copyable.h"

#include "asio/io_service.hpp"
#include "asio/high_resolution_timer.hpp"

#include <functional>
#include <optional>
#include <queue>


namespace mmo
{
	/// Provides a class for managing timers.
	class TimerQueue
		: NonCopyable
	{
	public:
		/// Callback that is executed on expiration of a timer that is still valid.
		typedef std::function<void ()> EventCallback;

	public:
		/// Explicit default constructor.
		/// @param service The io service object to queue timers in to.
		explicit TimerQueue(asio::io_service &service);

	public:
		/// Gets the current timestamp in milliseconds.
		GameTime GetNow() const;

		/// Adds a new event to the timer queue to expire at a given timestamp value.
		/// @param callback The callback to be executed on expiration.
		/// @param time The timestamp at which the event shoud expire.
		void AddEvent(const EventCallback& callback, GameTime time);

		template<class T, class Type, class Result, class... Args>
		void AddEvent(const GameTime time, T& instance, Result(Type::*method), Args&&... args)
		{
			auto request = std::bind(method, &instance, std::forward<Args>(args)...);
			AddEvent(std::move(request), time);
		}

	private:
		/// Contains data of a queued event entry.
		struct EventEntry
		{
			/// The callback to execute.
			EventCallback callback;
			/// The timestamp at which the event has been queued.
			GameTime time;

			/// Helper for comparison purposes.
			struct IsLater
			{
				inline bool operator ()(const EventEntry &left, const EventEntry &right) const
				{
					return (left.time > right.time);
				}
			};

			EventEntry()
			{
			}

			EventEntry(const EventCallback &callback, GameTime time)
				: callback(callback)
				, time(time)
			{
			}
		};

		typedef std::priority_queue<EventEntry, std::vector<EventEntry>, EventEntry::IsLater> Queue;
		typedef asio::high_resolution_timer Timer;

		Timer m_timer;
		std::optional<GameTime> m_timerTime;
		Queue m_queue;

	private:
		void Update(const asio::system_error &error);
		void SetTimer();
	};
}
