// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_client/vigor_predictor.h"

using namespace mmo;

TEST_CASE("Predictor re-anchors on server value", "[vigor]")
{
	VigorPredictor predictor;
	CHECK_FALSE(predictor.IsAnchored());

	// The glider is already open when the first value arrives: no start cost.
	predictor.Advance(0, VigorActivity::Gliding, 0.0f, false, false);
	predictor.Anchor(100.0f, 100.0f, 1000);
	CHECK(predictor.IsAnchored());
	CHECK(predictor.GetCurrent() == Approx(100.0f));
	CHECK(predictor.GetMax() == Approx(100.0f));

	// Two seconds of gliding drain 8 per second.
	predictor.Advance(3000, VigorActivity::Gliding, 0.0f, false, false);
	CHECK(predictor.GetCurrent() == Approx(84.0f));

	// The server's value wins, and the prediction continues from it.
	predictor.Anchor(90.0f, 100.0f, 3000);
	CHECK(predictor.GetCurrent() == Approx(90.0f));

	predictor.Advance(4000, VigorActivity::GlidingInUpdraft, 0.0f, false, false);
	CHECK(predictor.GetCurrent() == Approx(86.0f));

	// A Dash aura drains on the ground on top of nothing else.
	predictor.Advance(5000, VigorActivity::Draining, 12.0f, false, true);
	CHECK(predictor.GetCurrent() == Approx(74.0f));
}

TEST_CASE("Predictor keeps the regeneration delay across a re-anchor", "[vigor]")
{
	VigorPredictor predictor;
	predictor.Anchor(50.0f, 100.0f, 0);

	// Draining until 1000, then grounded: the server writes on landing.
	predictor.Advance(1000, VigorActivity::Gliding, 0.0f, false, false);
	predictor.Anchor(42.0f, 100.0f, 1000);

	// Still inside the 1 s regeneration delay.
	predictor.Advance(1500, VigorActivity::Idle, 0.0f, false, true);
	CHECK(predictor.GetCurrent() == Approx(42.0f));

	// Half a second past the delay: 25 per second.
	predictor.Advance(2500, VigorActivity::Idle, 0.0f, false, true);
	CHECK(predictor.GetCurrent() == Approx(54.5f));

	// No regeneration in combat or while airborne.
	predictor.Advance(3500, VigorActivity::Idle, 0.0f, true, true);
	predictor.Advance(4500, VigorActivity::Idle, 0.0f, false, false);
	CHECK(predictor.GetCurrent() == Approx(54.5f));
}

TEST_CASE("Predictor regenerates at once after the first anchor", "[vigor]")
{
	// Nothing drained since the character entered the world, so no regeneration delay applies.
	VigorPredictor predictor;
	predictor.Anchor(50.0f, 100.0f, 0);
	predictor.Advance(1000, VigorActivity::Idle, 0.0f, false, true);
	CHECK(predictor.GetCurrent() == Approx(75.0f));
}

TEST_CASE("Predictor does not advance before it is anchored", "[vigor]")
{
	VigorPredictor predictor;
	predictor.Advance(5000, VigorActivity::Gliding, 0.0f, false, false);
	CHECK_FALSE(predictor.IsAnchored());
	CHECK_FALSE(predictor.ConsumeBecameEmpty());
}

TEST_CASE("Predictor reports empty once", "[vigor]")
{
	VigorPredictor predictor;
	predictor.Advance(0, VigorActivity::Gliding, 0.0f, false, false);
	predictor.Anchor(4.0f, 100.0f, 0);
	CHECK_FALSE(predictor.ConsumeBecameEmpty());

	predictor.Advance(250, VigorActivity::Gliding, 0.0f, false, false);
	CHECK(predictor.GetCurrent() == Approx(2.0f));
	CHECK_FALSE(predictor.ConsumeBecameEmpty());

	predictor.Advance(1000, VigorActivity::Gliding, 0.0f, false, false);
	CHECK(predictor.GetCurrent() == Approx(0.0f));
	CHECK(predictor.ConsumeBecameEmpty());
	CHECK_FALSE(predictor.ConsumeBecameEmpty());

	// Staying empty does not report again.
	predictor.Advance(2000, VigorActivity::Gliding, 0.0f, false, false);
	CHECK_FALSE(predictor.ConsumeBecameEmpty());

	// The server confirms 0: still no second report.
	predictor.Anchor(0.0f, 100.0f, 2000);
	CHECK_FALSE(predictor.ConsumeBecameEmpty());

	// Once it rose above zero, running empty again reports again.
	predictor.Anchor(1.0f, 100.0f, 2000);
	predictor.Advance(3000, VigorActivity::Gliding, 0.0f, false, false);
	CHECK(predictor.ConsumeBecameEmpty());
}

TEST_CASE("Predictor notifies changes at most 10 times per second", "[vigor]")
{
	VigorPredictor predictor;
	predictor.Anchor(100.0f, 100.0f, 0);

	// The first value is always reported.
	CHECK(predictor.ConsumeChangeNotification(0));
	CHECK_FALSE(predictor.ConsumeChangeNotification(16));

	// Gliding changes the value every frame, but only every 100 ms is reported.
	int notifications = 0;
	for (GameTime now = 10; now <= 1000; now += 10)
	{
		predictor.Advance(now, VigorActivity::Gliding, 0.0f, false, false);
		if (predictor.ConsumeChangeNotification(now))
		{
			++notifications;
		}
	}
	CHECK(notifications <= 10);
	CHECK(notifications == 10);

	// A change that was held back is reported once the interval passed, even without a new change.
	predictor.Anchor(50.0f, 100.0f, 1001);
	const bool reportedAtOnce = predictor.ConsumeChangeNotification(1001);
	if (!reportedAtOnce)
	{
		CHECK(predictor.ConsumeChangeNotification(1200));
	}
	CHECK_FALSE(predictor.ConsumeChangeNotification(1400));

	// No change, no notification.
	CHECK_FALSE(predictor.ConsumeChangeNotification(5000));
}

TEST_CASE("Predictor applies the glide start cost when the glider opens", "[vigor]")
{
	VigorPredictor predictor;
	predictor.Anchor(50.0f, 100.0f, 0);

	// Falling, glider closed: nothing drains.
	predictor.Advance(500, VigorActivity::Idle, 0.0f, false, false);
	CHECK(predictor.GetCurrent() == Approx(50.0f));

	// Opening the glider costs 5 at once, plus half a second of glide drain (8 per second).
	predictor.Advance(1000, VigorActivity::Gliding, 0.0f, false, false);
	CHECK(predictor.GetCurrent() == Approx(41.0f));

	// Gliding on costs no further start cost, also not across a server write that contains it.
	predictor.Anchor(41.0f, 100.0f, 1000);
	predictor.Advance(1500, VigorActivity::GlidingInUpdraft, 0.0f, false, false);
	CHECK(predictor.GetCurrent() == Approx(39.0f));

	// Closing and reopening the glider costs again; the cost alone may empty the prediction.
	predictor.Anchor(3.0f, 100.0f, 2000);
	predictor.Advance(2000, VigorActivity::Idle, 0.0f, false, false);
	(void)predictor.ConsumeBecameEmpty();
	predictor.Advance(2000, VigorActivity::Gliding, 0.0f, false, false);
	CHECK(predictor.GetCurrent() == Approx(0.0f));
	CHECK(predictor.ConsumeBecameEmpty());
}

TEST_CASE("The predictor drains at the glider's rate after SetSettings", "[vigor_predictor]")
{
	TraversalSettings enduring;
	enduring.glideDrainPerSecond = 2.0f;

	VigorPredictor predictor;
	predictor.SetSettings(enduring);
	predictor.Anchor(100.0f, 100.0f, 0);
	predictor.Advance(0, VigorActivity::Idle, 0.0f, false, false);

	// Opening the glider costs 5, then one second at 2.
	predictor.Advance(1000, VigorActivity::Gliding, 0.0f, false, false);
	CHECK(predictor.GetCurrent() == Approx(93.0f));
}
