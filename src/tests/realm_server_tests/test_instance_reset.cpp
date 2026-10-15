// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Resetting dungeon instances is a request the realm has to decide on before it releases
// anything: only a group leader decides for a group, and an instance may never be released
// while somebody is still inside it.

#include "catch.hpp"

#include "realm_server/instance_reset.h"

using namespace mmo;

namespace
{
	InstanceId makeInstanceId()
	{
		uuids::uuid_system_generator generator;
		return generator();
	}

	/// Nobody is inside any instance.
	std::vector<uint64> noOccupants(InstanceId)
	{
		return {};
	}
}

TEST_CASE("A solo character resets every instance it is bound to", "[instance_reset]")
{
	const InstanceId firstInstance = makeInstanceId();
	const InstanceId secondInstance = makeInstanceId();

	const std::map<MapId, InstanceId> bindings{ { 530, firstInstance }, { 531, secondInstance } };

	const InstanceResetPlan plan = PlanInstanceReset(bindings, false, true, noOccupants);

	REQUIRE(plan.rejection == instance_reset_rejection::None);
	REQUIRE(plan.blocked.empty());
	REQUIRE(plan.reset.size() == 2);
	CHECK(plan.reset[0].mapId == 530);
	CHECK(plan.reset[0].instanceId == firstInstance);
	CHECK(plan.reset[1].mapId == 531);
	CHECK(plan.reset[1].instanceId == secondInstance);
}

TEST_CASE("An instance someone is still inside of is not reset", "[instance_reset]")
{
	const InstanceId occupiedInstance = makeInstanceId();
	const InstanceId emptyInstance = makeInstanceId();

	const std::map<MapId, InstanceId> bindings{ { 530, occupiedInstance }, { 531, emptyInstance } };

	const InstanceResetPlan plan = PlanInstanceReset(bindings, false, true,
		[&occupiedInstance](const InstanceId instanceId) -> std::vector<uint64>
		{
			if (instanceId == occupiedInstance)
			{
				return { 0x11, 0x22 };
			}

			return {};
		});

	REQUIRE(plan.rejection == instance_reset_rejection::None);

	// The empty one is still released: only the occupied instance is held back, and it names
	// the characters which have to leave first.
	REQUIRE(plan.reset.size() == 1);
	CHECK(plan.reset[0].instanceId == emptyInstance);

	REQUIRE(plan.blocked.size() == 1);
	CHECK(plan.blocked[0].mapId == 530);
	CHECK(plan.blocked[0].instanceId == occupiedInstance);
	REQUIRE(plan.blocked[0].occupants.size() == 2);
	CHECK(plan.blocked[0].occupants[0] == 0x11);
	CHECK(plan.blocked[0].occupants[1] == 0x22);
}

TEST_CASE("A group member which does not lead the group may not reset", "[instance_reset]")
{
	const std::map<MapId, InstanceId> bindings{ { 530, makeInstanceId() } };

	const InstanceResetPlan plan = PlanInstanceReset(bindings, false, false, noOccupants);

	CHECK(plan.rejection == instance_reset_rejection::NotGroupLeader);
	CHECK(plan.reset.empty());
	CHECK(plan.blocked.empty());
}

TEST_CASE("A character inside a dungeon may not reset instances", "[instance_reset]")
{
	const std::map<MapId, InstanceId> bindings{ { 530, makeInstanceId() } };

	// Even though the occupant lookup reports nobody inside, the requester's own position is
	// enough to refuse: it is standing in the instance it wants to release.
	const InstanceResetPlan plan = PlanInstanceReset(bindings, true, true, noOccupants);

	CHECK(plan.rejection == instance_reset_rejection::InsideInstance);
	CHECK(plan.reset.empty());
	CHECK(plan.blocked.empty());
}

TEST_CASE("A character without any binding has nothing to reset", "[instance_reset]")
{
	const InstanceResetPlan plan = PlanInstanceReset({}, false, true, noOccupants);

	CHECK(plan.rejection == instance_reset_rejection::NothingBound);
	CHECK(plan.reset.empty());
	CHECK(plan.blocked.empty());
}
