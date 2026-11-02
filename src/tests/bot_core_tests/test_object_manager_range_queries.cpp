// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "bot_core/bot_object_manager.h"

using namespace mmo;

namespace
{
	constexpr uint64 SelfGuid = 1;

	BotUnit Creature(const uint64 guid, const Vector3& position, const uint64 targetGuid)
	{
		BotUnit unit(guid, ObjectTypeId::Unit);
		MovementInfo info;
		info.position = position;
		unit.SetMovementInfo(info);
		unit.SetTargetGuid(targetGuid);
		unit.SetHealth(100);
		return unit;
	}

	/// A bot whose own unit still stands where it logged in: the server never echoes a client
	/// its own movement, so only the caller knows where the bot really is.
	void AddSelfAtLoginSpot(BotObjectManager& objects)
	{
		objects.SetSelfGuid(SelfGuid);

		BotUnit self(SelfGuid, ObjectTypeId::Player);
		MovementInfo login;
		login.position = Vector3(0.0f, 0.0f, 0.0f);
		self.SetMovementInfo(login);
		self.SetHealth(100);
		objects.AddOrUpdateUnit(self);
	}
}

TEST_CASE("Units targeting the bot are counted around its live position, not its login spot", "[bot_core][object_manager]")
{
	BotObjectManager objects;
	AddSelfAtLoginSpot(objects);

	// The bot has walked 200 yards out of town since it logged in.
	const Vector3 live(200.0f, 0.0f, 0.0f);

	// A mob that aggroed as the bot walked past and now trails it, out of swing range.
	objects.AddOrUpdateUnit(Creature(10, Vector3(215.0f, 0.0f, 0.0f), SelfGuid));
	// A guard in town, next to the login spot, with the bot as its target.
	objects.AddOrUpdateUnit(Creature(11, Vector3(5.0f, 0.0f, 0.0f), SelfGuid));
	// A mob near the bot that is fighting someone else.
	objects.AddOrUpdateUnit(Creature(12, Vector3(205.0f, 0.0f, 0.0f), 99));

	const auto attackers = objects.GetUnitsTargetingSelf(live, 40.0f);
	REQUIRE(attackers.size() == 1);
	CHECK(attackers.front()->GetGuid() == 10);

	// Without a range limit, everything targeting the bot counts, wherever it stands.
	CHECK(objects.GetUnitsTargetingSelf(live).size() == 2);
}

TEST_CASE("Nearest-unit queries measure from the live position", "[bot_core][object_manager]")
{
	BotObjectManager objects;
	AddSelfAtLoginSpot(objects);

	const Vector3 live(200.0f, 0.0f, 0.0f);
	objects.AddOrUpdateUnit(Creature(10, Vector3(210.0f, 0.0f, 0.0f), 0));
	objects.AddOrUpdateUnit(Creature(11, Vector3(5.0f, 0.0f, 0.0f), 0));

	const BotUnit* nearest = objects.GetNearestAttackable(live, 40.0f);
	REQUIRE(nearest != nullptr);
	CHECK(nearest->GetGuid() == 10);

	// The exclusion skips a creature the caller has given up on.
	CHECK(objects.GetNearestAttackable(live, 40.0f, [](const BotUnit& unit) { return unit.GetGuid() == 10; }) == nullptr);
}
