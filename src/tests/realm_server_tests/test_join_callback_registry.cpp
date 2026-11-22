// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "realm_server/join_callback_registry.h"

using namespace mmo;

namespace
{
	constexpr uint64 characterGuid = 0x1234;
}

TEST_CASE("A join answer without a pending request is ignored", "[join_callback]")
{
	JoinCallbackRegistry registry;

	CHECK_FALSE(registry.Consume(characterGuid, InstanceId(), true));
	CHECK_FALSE(registry.Consume(characterGuid, InstanceId(), false));
}

TEST_CASE("A join answer reaches the pending request once", "[join_callback]")
{
	JoinCallbackRegistry registry;
	int calls = 0;
	bool lastSuccess = false;

	registry.Register(characterGuid, [&](InstanceId, const bool success)
	{
		++calls;
		lastSuccess = success;
	});

	CHECK(registry.Consume(characterGuid, InstanceId(), true));
	CHECK(calls == 1);
	CHECK(lastSuccess);

	// A second answer for the same request (the world node answered a duplicate join) finds nothing.
	CHECK_FALSE(registry.Consume(characterGuid, InstanceId(), true));
	CHECK(calls == 1);
}

TEST_CASE("A repeated join request replaces the stale one", "[join_callback]")
{
	JoinCallbackRegistry registry;
	int staleCalls = 0;
	int currentCalls = 0;

	registry.Register(characterGuid, [&](InstanceId, bool) { ++staleCalls; });
	registry.Register(characterGuid, [&](InstanceId, bool) { ++currentCalls; });

	CHECK(registry.Consume(characterGuid, InstanceId(), true));
	CHECK_FALSE(registry.Consume(characterGuid, InstanceId(), true));

	CHECK(staleCalls == 0);
	CHECK(currentCalls == 1);
}

TEST_CASE("Join requests of different characters do not interfere", "[join_callback]")
{
	JoinCallbackRegistry registry;
	int firstCalls = 0;
	int secondCalls = 0;

	registry.Register(1, [&](InstanceId, bool) { ++firstCalls; });
	registry.Register(2, [&](InstanceId, bool) { ++secondCalls; });

	CHECK(registry.Consume(2, InstanceId(), false));
	CHECK(firstCalls == 0);
	CHECK(secondCalls == 1);
	CHECK(registry.Consume(1, InstanceId(), true));
	CHECK(firstCalls == 1);
}

TEST_CASE("An empty callback is not registered", "[join_callback]")
{
	JoinCallbackRegistry registry;
	registry.Register(characterGuid, nullptr);

	CHECK_FALSE(registry.Consume(characterGuid, InstanceId(), true));
}
