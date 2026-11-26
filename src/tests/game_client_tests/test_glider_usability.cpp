// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// When the own client may open the glider: the hidden base spell, an equipped glider, Vigor,
// alive and free to move.

#include "catch.hpp"

#include "game_client/glider_usability.h"

using namespace mmo;

namespace
{
	GliderUsabilityInput Ready()
	{
		GliderUsabilityInput input;
		input.knowsGlider = true;
		input.hasGliderEquipped = true;
		input.predictedVigor = 50.0f;
		input.alive = true;
		input.rooted = false;
		input.serverControlled = false;
		return input;
	}
}

TEST_CASE("the glider opens with spell, glider, vigor and free movement", "[glider]")
{
	CHECK(EvaluateGliderUsability(Ready()) == glider_usability::Usable);
}

TEST_CASE("without an equipped glider the glider does not open and says why", "[glider]")
{
	auto input = Ready();
	input.hasGliderEquipped = false;
	CHECK(EvaluateGliderUsability(input) == glider_usability::NoGliderEquipped);
}

TEST_CASE("the glider does not open while rooted, dead, out of vigor or without the spell", "[glider]")
{
	auto rooted = Ready();
	rooted.rooted = true;
	CHECK(EvaluateGliderUsability(rooted) == glider_usability::Unavailable);

	auto dead = Ready();
	dead.alive = false;
	CHECK(EvaluateGliderUsability(dead) == glider_usability::Unavailable);

	auto empty = Ready();
	empty.predictedVigor = 0.0f;
	CHECK(EvaluateGliderUsability(empty) == glider_usability::Unavailable);

	auto unknown = Ready();
	unknown.knowsGlider = false;
	CHECK(EvaluateGliderUsability(unknown) == glider_usability::Unavailable);
}
