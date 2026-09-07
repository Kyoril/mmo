// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "bot_ai/bot_ai_registry.h"
#include "bot_ai/bot_engine.h"
#include "bot_ai/strategies/grind_strategy.h"

#include <sstream>

using namespace mmo;

TEST_CASE("The grind strategies refer only to things that exist", "[bot_ai][grind]")
{
	BotAiRegistry registry;
	RegisterGrindStrategies(registry);

	const std::vector<std::string> dangling = registry.FindDanglingReferences();

	// A typo in a strategy table is invisible at runtime: the bot simply never does that one
	// thing, forever, and nothing is logged. This is the only place it can be caught cheaply.
	std::ostringstream detail;
	for (const std::string& name : dangling)
	{
		detail << name << " ";
	}

	INFO("dangling references: " << detail.str());
	CHECK(dangling.empty());
}

TEST_CASE("Every grind engine resolves its strategy", "[bot_ai][grind]")
{
	BotAiRegistry registry;
	RegisterGrindStrategies(registry);

	for (const char* name : { "grind_world", "grind_combat", "grind_dead" })
	{
		BotEngine engine(registry, name);
		engine.AddStrategy(name);

		// AddStrategy ignores an unknown name rather than failing, so the strategy list is what
		// says whether it actually found anything.
		INFO("strategy " << name);
		CHECK(engine.GetStrategyNames().size() == 1);
	}
}
