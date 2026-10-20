// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "bot_core/bot_context.h"

using namespace mmo;

namespace
{
	BotContext MakeContext()
	{
		const BotConfig config;
		return BotContext(nullptr, config, nullptr);
	}
}

TEST_CASE("A fresh bot context knows no map", "[bot_context][map_id]")
{
	BotContext context = MakeContext();

	CHECK_FALSE(context.HasCurrentMapId());
}

TEST_CASE("Entering map zero is a known map id", "[bot_context][map_id]")
{
	// Map 0 is the default world every e2e character spawns on. Treating the id as the
	// "unknown" marker made the bot fall back to guessing its map from the navigation mesh,
	// which happily resolves the start and the target on a different map whose mesh also
	// covers those coordinates - and then walks a path that does not exist where the bot is.
	BotContext context = MakeContext();

	context.UpdateCurrentMapIdFromWorldSync(0);

	CHECK(context.HasCurrentMapId());
	CHECK(context.GetCurrentMapId() == 0);
}

TEST_CASE("Setting map zero explicitly is a known map id", "[bot_context][map_id]")
{
	BotContext context = MakeContext();

	context.SetCurrentMapId(0);

	CHECK(context.HasCurrentMapId());
	CHECK(context.GetCurrentMapId() == 0);
}

TEST_CASE("A world sync back to map zero replaces the previous map", "[bot_context][map_id]")
{
	BotContext context = MakeContext();
	context.SetCurrentMapId(3);
	REQUIRE(context.GetCurrentMapId() == 3);

	context.UpdateCurrentMapIdFromWorldSync(0);

	CHECK(context.HasCurrentMapId());
	CHECK(context.GetCurrentMapId() == 0);
}

TEST_CASE("A non-zero map id is kept", "[bot_context][map_id]")
{
	BotContext context = MakeContext();

	context.UpdateCurrentMapIdFromWorldSync(3);

	CHECK(context.HasCurrentMapId());
	CHECK(context.GetCurrentMapId() == 3);
}
