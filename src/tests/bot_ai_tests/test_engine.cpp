// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "engine_fixture.h"

using namespace mmo;
using namespace mmo::bot_ai_tests;

TEST_CASE("A fired trigger runs its action", "[bot_ai][engine]")
{
	EngineFixture fixture;
	fixture.AddAction("attack");
	fixture.AddTrigger("enemy_near", true);

	ScriptedStrategy& strategy = fixture.AddStrategy("combat");
	strategy.nodes = { { "enemy_near", { { "attack", bot_relevance::Normal } } } };

	fixture.Build("combat");

	CHECK(fixture.Tick(0));
	CHECK(fixture.log.executed == std::vector<std::string>{ "attack" });
}

TEST_CASE("A trigger that does not fire runs nothing", "[bot_ai][engine]")
{
	EngineFixture fixture;
	fixture.AddAction("attack");
	fixture.AddTrigger("enemy_near", false);

	ScriptedStrategy& strategy = fixture.AddStrategy("combat");
	strategy.nodes = { { "enemy_near", { { "attack", bot_relevance::Normal } } } };

	fixture.Build("combat");

	CHECK_FALSE(fixture.Tick(0));
	CHECK(fixture.log.executed.empty());
}

TEST_CASE("Only one action runs per tick", "[bot_ai][engine]")
{
	EngineFixture fixture;
	fixture.AddAction("flee");
	fixture.AddAction("attack");
	fixture.AddTrigger("always", true);

	ScriptedStrategy& strategy = fixture.AddStrategy("combat");
	strategy.nodes = { { "always", { { "attack", bot_relevance::Normal }, { "flee", bot_relevance::Emergency } } } };

	fixture.Build("combat");

	// The emergency outranks the ordinary action, and the ordinary action does not get a
	// consolation run in the same tick - it stays queued for the next one.
	CHECK(fixture.Tick(0));
	CHECK(fixture.log.executed == std::vector<std::string>{ "flee" });

	CHECK(fixture.Tick(100));
	CHECK(fixture.log.executed == std::vector<std::string>{ "flee", "flee" });
}

TEST_CASE("A multiplier returning zero vetoes an action", "[bot_ai][engine]")
{
	EngineFixture fixture;
	fixture.AddAction("attack");
	fixture.AddAction("wander");
	fixture.AddTrigger("always", true);
	fixture.AddMultiplier("pacifist", [](const std::string& action) { return action == "attack" ? 0.0f : 1.0f; });

	ScriptedStrategy& strategy = fixture.AddStrategy("combat");
	strategy.nodes = { { "always", { { "attack", bot_relevance::Emergency }, { "wander", bot_relevance::Idle } } } };
	strategy.multipliers = { "pacifist" };

	fixture.Build("combat");

	// The vetoed action was by far the most relevant; the engine has to fall through to the
	// next one rather than doing nothing this tick.
	CHECK(fixture.Tick(0));
	CHECK(fixture.log.executed == std::vector<std::string>{ "wander" });
}

TEST_CASE("A multiplier can reorder two actions without vetoing either", "[bot_ai][engine]")
{
	EngineFixture fixture;
	fixture.AddAction("attack");
	fixture.AddAction("heal");
	fixture.AddTrigger("always", true);
	fixture.AddMultiplier("wounded", [](const std::string& action) { return action == "heal" ? 5.0f : 1.0f; });

	ScriptedStrategy& strategy = fixture.AddStrategy("combat");
	strategy.nodes = { { "always", { { "attack", bot_relevance::High }, { "heal", bot_relevance::Normal } } } };
	strategy.multipliers = { "wounded" };

	fixture.Build("combat");

	CHECK(fixture.Tick(0));
	CHECK(fixture.log.executed == std::vector<std::string>{ "heal" });
}

TEST_CASE("An action that would accomplish nothing is skipped", "[bot_ai][engine]")
{
	EngineFixture fixture;
	ScriptedAction& attack = fixture.AddAction("attack");
	attack.useful = false;
	fixture.AddAction("wander");
	fixture.AddTrigger("always", true);

	ScriptedStrategy& strategy = fixture.AddStrategy("combat");
	strategy.nodes = { { "always", { { "attack", bot_relevance::Emergency }, { "wander", bot_relevance::Idle } } } };

	fixture.Build("combat");

	CHECK(fixture.Tick(0));
	CHECK(fixture.log.executed == std::vector<std::string>{ "wander" });
}

TEST_CASE("An impossible action runs its prerequisite first, then itself", "[bot_ai][engine]")
{
	EngineFixture fixture;
	ScriptedAction& swing = fixture.AddAction("swing");
	swing.possible = false;
	swing.prerequisites = { { "face_target", bot_relevance::Normal } };
	fixture.AddAction("face_target");
	fixture.AddTrigger("always", true);

	ScriptedStrategy& strategy = fixture.AddStrategy("combat");
	strategy.nodes = { { "always", { { "swing", bot_relevance::Normal } } } };

	fixture.Build("combat");

	// Tick one: the swing cannot happen, so the thing that makes it possible runs instead.
	CHECK(fixture.Tick(0));
	CHECK(fixture.log.executed == std::vector<std::string>{ "face_target" });

	// The prerequisite worked, which the world only reflects on the next tick.
	swing.possible = true;
	CHECK(fixture.Tick(100));
	CHECK(fixture.log.executed == std::vector<std::string>{ "face_target", "swing" });
}

TEST_CASE("An impossible action with no prerequisite is dropped rather than spinning", "[bot_ai][engine]")
{
	EngineFixture fixture;
	ScriptedAction& swing = fixture.AddAction("swing");
	swing.possible = false;
	fixture.AddAction("wander");
	fixture.AddTrigger("always", true);

	ScriptedStrategy& strategy = fixture.AddStrategy("combat");
	strategy.nodes = { { "always", { { "swing", bot_relevance::Emergency }, { "wander", bot_relevance::Idle } } } };

	fixture.Build("combat");

	CHECK(fixture.Tick(0));
	CHECK(fixture.log.executed == std::vector<std::string>{ "wander" });
}

TEST_CASE("A failed action falls back to its alternative", "[bot_ai][engine]")
{
	EngineFixture fixture;
	ScriptedAction& cast = fixture.AddAction("cast_fireball");
	cast.succeeds = false;
	cast.alternatives = { { "melee", bot_relevance::Normal } };
	fixture.AddAction("melee");
	fixture.AddTrigger("always", true);

	ScriptedStrategy& strategy = fixture.AddStrategy("combat");
	strategy.nodes = { { "always", { { "cast_fireball", bot_relevance::Normal } } } };

	fixture.Build("combat");

	// Both run in the same tick: a failed action did not consume the tick, so the engine keeps
	// looking. Only a successful action ends the tick.
	CHECK(fixture.Tick(0));
	CHECK(fixture.log.executed == std::vector<std::string>{ "cast_fireball", "melee" });
}

TEST_CASE("A successful action queues its continuers for the next tick", "[bot_ai][engine]")
{
	EngineFixture fixture;
	ScriptedAction& pull = fixture.AddAction("pull");
	pull.continuers = { { "engage", bot_relevance::High } };
	fixture.AddAction("engage");
	ScriptedTrigger& trigger = fixture.AddTrigger("target_spotted", true);

	ScriptedStrategy& strategy = fixture.AddStrategy("combat");
	strategy.nodes = { { "target_spotted", { { "pull", bot_relevance::Normal } } } };

	fixture.Build("combat");

	CHECK(fixture.Tick(0));
	CHECK(fixture.log.executed == std::vector<std::string>{ "pull" });

	// The trigger has stopped firing, but the chain carries on by itself - which is the point of
	// continuers, and what lets a multi-step behaviour outlive the condition that started it.
	trigger.active = false;
	CHECK(fixture.Tick(100));
	CHECK(fixture.log.executed == std::vector<std::string>{ "pull", "engage" });
}

TEST_CASE("A trigger with a check interval is not polled every tick", "[bot_ai][engine]")
{
	EngineFixture fixture;
	fixture.AddAction("scan");
	ScriptedTrigger& trigger = fixture.AddTrigger("expensive_scan", true, 1000);

	ScriptedStrategy& strategy = fixture.AddStrategy("idle");
	strategy.nodes = { { "expensive_scan", { { "scan", bot_relevance::Normal } } } };

	fixture.Build("idle");

	fixture.Tick(0);
	CHECK(trigger.checkCount == 1);

	fixture.Tick(500);
	CHECK(trigger.checkCount == 1);

	fixture.Tick(1000);
	CHECK(trigger.checkCount == 2);
}

TEST_CASE("Default actions run when no trigger fires", "[bot_ai][engine]")
{
	EngineFixture fixture;
	fixture.AddAction("attack");
	fixture.AddAction("idle_around");
	fixture.AddTrigger("enemy_near", false);

	ScriptedStrategy& strategy = fixture.AddStrategy("world");
	strategy.nodes = { { "enemy_near", { { "attack", bot_relevance::Normal } } } };
	strategy.defaults = { { "idle_around", bot_relevance::Idle } };

	fixture.Build("world");

	CHECK(fixture.Tick(0));
	CHECK(fixture.log.executed == std::vector<std::string>{ "idle_around" });
}

TEST_CASE("Resetting the engine drops the plan it had made", "[bot_ai][engine]")
{
	EngineFixture fixture;
	fixture.AddAction("attack");
	fixture.AddAction("wander");
	ScriptedTrigger& trigger = fixture.AddTrigger("always", true);

	ScriptedStrategy& strategy = fixture.AddStrategy("combat");
	strategy.nodes = { { "always", { { "wander", bot_relevance::Idle }, { "attack", bot_relevance::Normal } } } };

	fixture.Build("combat");

	CHECK(fixture.Tick(0));
	CHECK(fixture.log.executed == std::vector<std::string>{ "attack" });
	CHECK(fixture.engine->GetQueue().FindRelevance("wander") >= 0.0f);

	// Swapping engines must not carry a plan made under different circumstances across, so the
	// queue is emptied rather than kept.
	trigger.active = false;
	fixture.engine->Reset();
	CHECK(fixture.engine->GetQueue().IsEmpty());
	CHECK_FALSE(fixture.Tick(100));
}

TEST_CASE("A strategy referring to something unregistered is reported", "[bot_ai][registry]")
{
	EngineFixture fixture;
	fixture.AddAction("attack");
	fixture.AddTrigger("enemy_near", true);

	ScriptedStrategy& strategy = fixture.AddStrategy("combat");
	strategy.nodes = {
		{ "enemy_near", { { "attack", bot_relevance::Normal }, { "atack", bot_relevance::Normal } } },
		{ "enemy_nea", { { "attack", bot_relevance::Normal } } },
	};
	strategy.multipliers = { "nonexistent" };

	const std::vector<std::string> dangling = fixture.registry.FindDanglingReferences();

	CHECK(std::find(dangling.begin(), dangling.end(), "action:atack") != dangling.end());
	CHECK(std::find(dangling.begin(), dangling.end(), "trigger:enemy_nea") != dangling.end());
	CHECK(std::find(dangling.begin(), dangling.end(), "multiplier:nonexistent") != dangling.end());
	CHECK(std::find(dangling.begin(), dangling.end(), "action:attack") == dangling.end());
}
