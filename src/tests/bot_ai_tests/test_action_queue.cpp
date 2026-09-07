// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "bot_ai/bot_action_queue.h"

using namespace mmo;

TEST_CASE("Queue pops the most relevant action first", "[bot_ai][queue]")
{
	BotActionQueue queue;
	queue.Push("wander", 5.0f, 0);
	queue.Push("flee", 90.0f, 0);
	queue.Push("attack", 10.0f, 0);

	BotActionBasket basket;
	REQUIRE(queue.Pop(basket));
	CHECK(basket.action == "flee");
	REQUIRE(queue.Pop(basket));
	CHECK(basket.action == "attack");
	REQUIRE(queue.Pop(basket));
	CHECK(basket.action == "wander");
	CHECK_FALSE(queue.Pop(basket));
}

TEST_CASE("Pushing the same action twice keeps the higher relevance", "[bot_ai][queue]")
{
	BotActionQueue queue;
	queue.Push("heal", 10.0f, 0);
	queue.Push("heal", 90.0f, 0);

	// Two strategies wanting the same thing must not make it run twice, and the more urgent
	// request has to win rather than the later one.
	CHECK(queue.GetSize() == 1);
	CHECK(queue.FindRelevance("heal") == Approx(90.0f));

	queue.Push("heal", 20.0f, 0);
	CHECK(queue.FindRelevance("heal") == Approx(90.0f));
}

TEST_CASE("Equally relevant actions pop in the order they were pushed", "[bot_ai][queue]")
{
	BotActionQueue queue;
	queue.Push("first", 10.0f, 0);
	queue.Push("second", 10.0f, 0);

	BotActionBasket basket;
	REQUIRE(queue.Pop(basket));
	CHECK(basket.action == "first");
}

TEST_CASE("Stale actions are dropped rather than run late", "[bot_ai][queue]")
{
	BotActionQueue queue;
	queue.Push("old", 10.0f, 500);
	queue.Push("fresh", 10.0f, 5500);

	queue.RemoveExpired(6000, 5000);

	CHECK(queue.GetSize() == 1);
	CHECK(queue.FindRelevance("old") < 0.0f);
	CHECK(queue.FindRelevance("fresh") == Approx(10.0f));
}

TEST_CASE("An action exactly at the expiry age survives", "[bot_ai][queue]")
{
	BotActionQueue queue;
	queue.Push("edge", 10.0f, 1000);

	queue.RemoveExpired(6000, 5000);
	CHECK(queue.GetSize() == 1);

	queue.RemoveExpired(6001, 5000);
	CHECK(queue.IsEmpty());
}
