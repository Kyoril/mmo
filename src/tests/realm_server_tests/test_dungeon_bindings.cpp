// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "realm_server/dungeon_bindings.h"

using namespace mmo;

namespace
{
	InstanceId makeInstanceId()
	{
		uuids::uuid_system_generator generator;
		return generator();
	}
}

TEST_CASE("Dungeon binding of a character is reused by that character", "[dungeon_bindings]")
{
	const InstanceId instance = makeInstanceId();

	DungeonBindings bindings;
	bindings.SetOwner(0x42);
	bindings.Set(530, instance);

	REQUIRE(bindings.Find(530) == instance);

	// Re-entering the world with the same character keeps the binding.
	bindings.SetOwner(0x42);
	REQUIRE(bindings.Find(530) == instance);
}

TEST_CASE("Another character on the same session does not inherit dungeon bindings", "[dungeon_bindings]")
{
	const InstanceId instance = makeInstanceId();

	DungeonBindings bindings;
	bindings.SetOwner(0x42);
	bindings.Set(530, instance);

	// The session stays alive while the player returns to character selection and enters the
	// world with a different character. That character must get its own dungeon instance.
	bindings.SetOwner(0x43);
	REQUIRE(bindings.Find(530).is_nil());
}

TEST_CASE("Dungeon bindings of a destroyed instance are removed", "[dungeon_bindings]")
{
	const InstanceId instance = makeInstanceId();
	const InstanceId otherInstance = makeInstanceId();

	DungeonBindings bindings;
	bindings.SetOwner(0x42);
	bindings.Set(530, instance);
	bindings.Set(531, otherInstance);

	bindings.RemoveByInstanceId(instance);

	REQUIRE(bindings.Find(530).is_nil());
	REQUIRE(bindings.Find(531) == otherInstance);

	bindings.Remove(531);
	REQUIRE(bindings.Find(531).is_nil());
}
