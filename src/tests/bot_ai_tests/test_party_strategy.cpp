// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "engine_fixture.h"

#include "bot_ai/bot_ai_context.h"
#include "bot_ai/bot_ai_registry.h"
#include "bot_ai/strategies/grind_strategy.h"
#include "bot_ai/strategies/party_strategy.h"

#include <sstream>

using namespace mmo;
using namespace mmo::bot_ai_tests;

TEST_CASE("Party strategies refer only to registered definitions", "[bot_ai][party]")
{
	BotAiRegistry registry;
	RegisterGrindStrategies(registry);
	RegisterPartyStrategies(registry);

	CHECK(registry.FindStrategy("party_world") != nullptr);
	CHECK(registry.FindStrategy("party_combat") != nullptr);

	std::ostringstream detail;
	for (const std::string& name : registry.FindDanglingReferences())
	{
		detail << name << ' ';
	}

	INFO("dangling: " << detail.str());
	CHECK(registry.FindDanglingReferences().empty());
}

TEST_CASE("Party triggers stay quiet while the bot plays alone", "[bot_ai][party]")
{
	BotAiRegistry registry;
	RegisterGrindStrategies(registry);
	RegisterPartyStrategies(registry);

	// No session and nobody to follow: nothing in a party table may fire.
	BotAiContext context(nullptr, 0);
	for (const char* name : { "leader_far", "leader_very_far", "leader_enemy_to_assist" })
	{
		const BotTrigger* trigger = registry.FindTrigger(name);
		REQUIRE(trigger != nullptr);
		CHECK_FALSE(trigger->IsActive(context));
	}

	context.GetFollowState().leaderGuid = 42;
	CHECK(context.GetFollowState().IsFollowing());
	CHECK_FALSE(registry.FindTrigger("leader_far")->IsActive(context));
}

TEST_CASE("Clearing an engine's strategies swaps its behaviour", "[bot_ai][party]")
{
	EngineFixture fixture;
	fixture.AddAction("grind");
	fixture.AddAction("follow");
	fixture.AddTrigger("always", true);

	fixture.AddStrategy("solo").nodes = { { "always", { { "grind", bot_relevance::Normal } } } };
	fixture.AddStrategy("party").nodes = { { "always", { { "follow", bot_relevance::Normal } } } };

	BotEngine& engine = fixture.Build("solo");
	CHECK(fixture.Tick(0));
	CHECK(fixture.log.Ran("grind"));

	engine.ClearStrategies();
	engine.AddStrategy("party");
	fixture.log.executed.clear();

	CHECK(fixture.Tick(1000));
	CHECK(fixture.log.executed == std::vector<std::string>{ "follow" });
	CHECK(engine.GetStrategyNames() == std::vector<std::string>{ "party" });
}
