// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Countdown is the primitive behind every server-side repeating timer: auto-attack swings,
// regeneration, despawns, spell casts. Its contract is subtle in one specific way -- it clears
// m_running *before* raising `ended`, so any handler that asks IsRunning() sees false and
// concludes nothing is scheduled. That is what let a single auto-attack swing arm the swing
// countdown twice (see GameUnitS::OnSpellCastEnded), so the behaviour is pinned down here.

#include "catch.hpp"

#include "base/countdown.h"
#include "base/timer_queue.h"

#include "asio/io_service.hpp"

using namespace mmo;

namespace
{
	/// Short enough to keep the suite fast, long enough that the timer does not expire before
	/// io_service::run() is entered.
	constexpr GameTime TinyDelayMs = 5;

	struct CountdownFixture
	{
		asio::io_service io;
		TimerQueue timers{ io };
		Countdown countdown{ timers };

		GameTime Soon() const { return timers.GetNow() + TinyDelayMs; }
	};
}

TEST_CASE_METHOD(CountdownFixture, "A countdown raises ended exactly once", "[countdown]")
{
	int fireCount = 0;
	const auto connection = countdown.ended.connect([&fireCount] { ++fireCount; });

	countdown.SetEnd(Soon());
	io.run();

	CHECK(fireCount == 1);
}

TEST_CASE_METHOD(CountdownFixture, "A countdown reports itself as not running while ended is raised", "[countdown]")
{
	// This is the trap. A handler that guards on IsRunning() to decide whether to re-arm will
	// always take the re-arm branch, because the countdown has already marked itself stopped.
	bool runningInsideHandler = true;
	const auto connection = countdown.ended.connect([this, &runningInsideHandler]
		{
			runningInsideHandler = countdown.IsRunning();
		});

	countdown.SetEnd(Soon());
	io.run();

	CHECK_FALSE(runningInsideHandler);
}

TEST_CASE_METHOD(CountdownFixture, "Re-arming from inside the ended handler raises ended again", "[countdown]")
{
	int fireCount = 0;
	const auto connection = countdown.ended.connect([this, &fireCount]
		{
			++fireCount;
			if (fireCount == 1)
			{
				countdown.SetEnd(Soon());
			}
		});

	countdown.SetEnd(Soon());
	io.run();

	CHECK(fireCount == 2);
}

TEST_CASE_METHOD(CountdownFixture, "Arming twice from inside the ended handler still raises ended only once more", "[countdown]")
{
	// The double-arm the auto-attack swing used to perform. The generation counter makes the
	// later SetEnd win, so the first scheduled event is silently superseded rather than
	// delivered -- correct, but it leaves a dead timer event behind on every single swing.
	// If this ever starts reporting 3, a swing would be resolving twice per interval.
	int fireCount = 0;
	const auto connection = countdown.ended.connect([this, &fireCount]
		{
			++fireCount;
			if (fireCount == 1)
			{
				countdown.SetEnd(Soon());
				countdown.SetEnd(Soon());
			}
		});

	countdown.SetEnd(Soon());
	io.run();

	CHECK(fireCount == 2);
}

TEST_CASE_METHOD(CountdownFixture, "Cancelling after re-arming inside the ended handler stops the countdown", "[countdown]")
{
	// StopAttack cancels the swing countdown, and it can run while a swing is still resolving.
	// The cancel has to win over a re-arm that already happened in the same handler.
	int fireCount = 0;
	const auto connection = countdown.ended.connect([this, &fireCount]
		{
			++fireCount;
			if (fireCount == 1)
			{
				countdown.SetEnd(Soon());
				countdown.Cancel();
			}
		});

	countdown.SetEnd(Soon());
	io.run();

	CHECK(fireCount == 1);
	CHECK_FALSE(countdown.IsRunning());
}

TEST_CASE_METHOD(CountdownFixture, "Cancelling before the deadline suppresses ended", "[countdown]")
{
	int fireCount = 0;
	const auto connection = countdown.ended.connect([&fireCount] { ++fireCount; });

	countdown.SetEnd(timers.GetNow() + 10000);
	countdown.Cancel();

	// The queue still holds the superseded event, and TimerQueue keeps its asio timer armed for
	// it, so draining with run() would block for the full ten seconds. Stop() drops the queue.
	timers.Stop();
	io.run();

	CHECK(fireCount == 0);
	CHECK_FALSE(countdown.IsRunning());
}

TEST_CASE_METHOD(CountdownFixture, "Re-arming a cancelled countdown works", "[countdown]")
{
	int fireCount = 0;
	const auto connection = countdown.ended.connect([&fireCount] { ++fireCount; });

	countdown.SetEnd(timers.GetNow() + 10000);
	countdown.Cancel();
	countdown.SetEnd(Soon());

	CHECK(countdown.IsRunning());

	io.run();

	CHECK(fireCount == 1);
}
