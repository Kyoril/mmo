// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// The glider's unfold animation: it opens and folds in GliderFoldState::FoldSeconds, reverses
// smoothly, and reports exactly one Opened and one Closed edge per visible cycle.

#include "catch.hpp"

#include "game_client/glider_fold_state.h"

using namespace mmo;

TEST_CASE("a new glider is folded and invisible", "[glider]")
{
	const GliderFoldState state;
	CHECK(state.GetUnfold() == 0.0f);
	CHECK_FALSE(state.IsVisible());
}

TEST_CASE("opening reports Opened once and unfolds over the fold time", "[glider]")
{
	GliderFoldState state;
	CHECK(state.Update(true, 0.1f) == glider_event::Opened);
	CHECK(state.GetUnfold() == Approx(0.4f));
	CHECK(state.IsVisible());

	CHECK(state.Update(true, 0.1f) == glider_event::None);
	CHECK(state.Update(true, 0.05f) == glider_event::None);
	CHECK(state.GetUnfold() == Approx(1.0f));

	CHECK(state.Update(true, 1.0f) == glider_event::None);
	CHECK(state.GetUnfold() == 1.0f);
}

TEST_CASE("closing reports Closed only when the glider is fully folded", "[glider]")
{
	GliderFoldState state;
	state.Update(true, 1.0f);

	CHECK(state.Update(false, 0.1f) == glider_event::None);
	CHECK(state.GetUnfold() == Approx(0.6f));
	CHECK(state.Update(false, 0.2f) == glider_event::Closed);
	CHECK(state.GetUnfold() == 0.0f);
	CHECK_FALSE(state.IsVisible());
	CHECK(state.Update(false, 0.1f) == glider_event::None);
}

TEST_CASE("re-opening while folding reverses from the current fraction without a second Opened", "[glider]")
{
	GliderFoldState state;
	state.Update(true, 1.0f);
	state.Update(false, 0.1f);
	REQUIRE(state.GetUnfold() == Approx(0.6f));

	CHECK(state.Update(true, 0.05f) == glider_event::None);
	CHECK(state.GetUnfold() == Approx(0.8f));
}

TEST_CASE("large frame times clamp the unfold to its range", "[glider]")
{
	GliderFoldState state;
	CHECK(state.Update(true, 10.0f) == glider_event::Opened);
	CHECK(state.GetUnfold() == 1.0f);
	CHECK(state.Update(false, 10.0f) == glider_event::Closed);
	CHECK(state.GetUnfold() == 0.0f);
}

TEST_CASE("a zero frame time while opening still reports Opened", "[glider]")
{
	GliderFoldState state;
	CHECK(state.Update(true, 0.0f) == glider_event::Opened);
	CHECK(state.IsVisible());
}
