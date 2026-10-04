// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "realm_server/shutdown_manager.h"
#include "game/shutdown_countdown.h"

#include <algorithm>
#include <vector>

using namespace mmo;

namespace
{
	/// Stands in for the realm's TimerQueue: events run in time order when time is advanced.
	struct FakeTimers
	{
		struct Event
		{
			std::function<void()> callback;
			GameTime time;
		};

		GameTime now = 1000000;
		std::vector<Event> events;

		ShutdownManager::Clock Clock()
		{
			return [this]() { return now; };
		}

		ShutdownManager::Scheduler Scheduler()
		{
			return [this](std::function<void()> callback, const GameTime time) { events.push_back({ std::move(callback), time }); };
		}

		/// Runs every event due up to and including `until`, earliest first.
		void AdvanceTo(const GameTime until)
		{
			while (true)
			{
				const auto next = std::min_element(events.begin(), events.end(),
					[](const Event& a, const Event& b) { return a.time < b.time; });
				if (next == events.end() || next->time > until)
				{
					break;
				}

				const Event event = *next;
				events.erase(next);
				now = std::max(now, event.time);
				event.callback();
			}

			now = until;
		}
	};

	struct Recorder
	{
		std::vector<uint32> announcements;
		uint32 dueCount = 0;
		scoped_connection announced;
		scoped_connection due;

		explicit Recorder(ShutdownManager& manager)
			: announced(manager.announce.connect([this](const uint32 seconds) { announcements.push_back(seconds); }))
			, due(manager.shutdownDue.connect([this]() { ++dueCount; }))
		{
		}
	};
}

TEST_CASE("A short shutdown announces immediately, at 0:15 and then becomes due", "[shutdown]")
{
	FakeTimers timers;
	ShutdownManager manager(timers.Clock(), timers.Scheduler());
	Recorder recorder(manager);
	const GameTime start = timers.now;

	manager.Schedule(20);
	CHECK(manager.IsPending());
	CHECK(manager.GetRemainingSeconds() == 20);
	CHECK(recorder.announcements == std::vector<uint32>{ 20 });

	timers.AdvanceTo(start + 5000);
	CHECK(recorder.announcements == std::vector<uint32>{ 20, 15 });
	CHECK(recorder.dueCount == 0);

	timers.AdvanceTo(start + 19999);
	CHECK(recorder.dueCount == 0);

	timers.AdvanceTo(start + 20000);
	CHECK(recorder.dueCount == 1);
	CHECK_FALSE(manager.IsPending());
}

TEST_CASE("A long shutdown walks the whole announcement schedule", "[shutdown]")
{
	FakeTimers timers;
	ShutdownManager manager(timers.Clock(), timers.Scheduler());
	Recorder recorder(manager);
	const GameTime start = timers.now;

	manager.Schedule(3700);
	timers.AdvanceTo(start + 3700 * 1000);

	CHECK(recorder.announcements == std::vector<uint32>{ 3700, 3600, 1800, 900, 600, 300, 240, 180, 120, 60, 45, 30, 15 });
	CHECK(recorder.dueCount == 1);
}

TEST_CASE("Cancelling announces it and silences every queued event", "[shutdown]")
{
	FakeTimers timers;
	ShutdownManager manager(timers.Clock(), timers.Scheduler());
	Recorder recorder(manager);
	const GameTime start = timers.now;

	manager.Schedule(60);
	CHECK(manager.Cancel());
	CHECK_FALSE(manager.IsPending());
	CHECK(manager.GetRemainingSeconds() == 0);
	CHECK(recorder.announcements == std::vector<uint32>{ 60, ShutdownCountdownCancelled });

	timers.AdvanceTo(start + 120 * 1000);
	CHECK(recorder.announcements.size() == 2);
	CHECK(recorder.dueCount == 0);

	CHECK_FALSE(manager.Cancel());
}

TEST_CASE("Scheduling again replaces the pending shutdown", "[shutdown]")
{
	FakeTimers timers;
	ShutdownManager manager(timers.Clock(), timers.Scheduler());
	Recorder recorder(manager);
	const GameTime start = timers.now;

	manager.Schedule(600);
	manager.Schedule(30);
	timers.AdvanceTo(start + 600 * 1000);

	CHECK(recorder.announcements == std::vector<uint32>{ 600, 30, 15 });
	CHECK(recorder.dueCount == 1);
}

TEST_CASE("A zero delay becomes due on the next timer run without announcing", "[shutdown]")
{
	FakeTimers timers;
	ShutdownManager manager(timers.Clock(), timers.Scheduler());
	Recorder recorder(manager);

	manager.Schedule(0);
	CHECK(recorder.dueCount == 0);

	timers.AdvanceTo(timers.now);
	CHECK(recorder.announcements.empty());
	CHECK(recorder.dueCount == 1);
}

TEST_CASE("Remaining seconds round up while a shutdown is pending", "[shutdown]")
{
	FakeTimers timers;
	ShutdownManager manager(timers.Clock(), timers.Scheduler());
	const GameTime start = timers.now;

	manager.Schedule(100);
	timers.now = start + 500;
	CHECK(manager.GetRemainingSeconds() == 100);
	timers.now = start + 1000;
	CHECK(manager.GetRemainingSeconds() == 99);
}
