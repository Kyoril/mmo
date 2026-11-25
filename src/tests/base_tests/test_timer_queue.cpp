// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// TimerQueue is shared by both login-server io threads, so it locks its members and runs due
// callbacks only after releasing that lock. These tests pin down the two consequences: a
// callback may schedule into the queue it is running from (a non-recursive lock held across the
// callback would deadlock here), and Stop() still drops events that were already collected for
// the running batch.

#include "catch.hpp"

#include "base/timer_queue.h"

#include "asio/io_service.hpp"

#include <chrono>
#include <thread>
#include <vector>

using namespace mmo;

namespace
{
	/// Short enough to keep the suite fast, long enough that the timer does not expire before
	/// io_service::run() is entered.
	constexpr GameTime TinyDelayMs = 5;

	struct TimerQueueFixture
	{
		asio::io_service io;
		TimerQueue timers{ io };

		GameTime Soon() const { return timers.GetNow() + TinyDelayMs; }
	};
}

TEST_CASE_METHOD(TimerQueueFixture, "A callback may add an event to the queue running it", "[timer_queue]")
{
	int fireCount = 0;
	timers.AddEvent([this, &fireCount]
		{
			++fireCount;
			timers.AddEvent([&fireCount] { ++fireCount; }, Soon());
		}, Soon());

	io.run();

	CHECK(fireCount == 2);
}

TEST_CASE_METHOD(TimerQueueFixture, "Stop from a callback drops the rest of the due batch", "[timer_queue]")
{
	// Waiting past both times before running makes both due when the timer fires, so they land in
	// the same batch; the later time keeps the Stop() event first.
	const GameTime due = Soon();
	bool secondRan = false;
	timers.AddEvent([this] { timers.Stop(); }, due);
	timers.AddEvent([&secondRan] { secondRan = true; }, due + 1);

	std::this_thread::sleep_for(std::chrono::milliseconds(TinyDelayMs * 4));
	io.run();

	CHECK_FALSE(secondRan);
}

TEST_CASE_METHOD(TimerQueueFixture, "Due events run in time order", "[timer_queue]")
{
	std::vector<int> order;
	const GameTime base = Soon();
	timers.AddEvent([&order] { order.push_back(2); }, base + 2);
	timers.AddEvent([&order] { order.push_back(1); }, base + 1);
	timers.AddEvent([&order] { order.push_back(3); }, base + 3);

	io.run();

	CHECK(order == std::vector<int>{ 1, 2, 3 });
}

TEST_CASE_METHOD(TimerQueueFixture, "AddEvent after Stop is ignored", "[timer_queue]")
{
	bool ran = false;
	timers.Stop();
	timers.AddEvent([&ran] { ran = true; }, Soon());

	io.run();

	CHECK_FALSE(ran);
}
