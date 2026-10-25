// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_server/quest_class_xp.h"
#include "shared/proto_data/classes.pb.h"

using namespace mmo;

namespace
{
	/// Builds a class entry with a linear class-level curve: xptonextlevel(L) = 400 * L.
	proto::ClassEntry makeLinearCurveClass(const uint32 levelCount)
	{
		proto::ClassEntry entry;
		for (uint32 i = 1; i <= levelCount; ++i)
		{
			entry.add_classlevels()->set_xptonextlevel(400 * i);
		}
		return entry;
	}
}

TEST_CASE("ScaleQuestClassXp - class level equal to quest level grants full XP", "[quest_class_xp]")
{
	const proto::ClassEntry entry = makeLinearCurveClass(10);
	REQUIRE(ScaleQuestClassXp(1000, 10, 10, entry) == 1000);
}

TEST_CASE("ScaleQuestClassXp - under-leveled class is rewarded as if the quest targeted its own level", "[quest_class_xp]")
{
	const proto::ClassEntry entry = makeLinearCurveClass(10);

	// Level-10 quest turned in at class level 1: same fraction of the level-1 bar
	// that the full reward would have been of the level-10 bar (400 / 4000).
	REQUIRE(ScaleQuestClassXp(3000, 1, 10, entry) == 300);

	// Class level 5: 2000 / 4000 of the reward.
	REQUIRE(ScaleQuestClassXp(3000, 5, 10, entry) == 1500);
}

TEST_CASE("ScaleQuestClassXp - under-leveled turn-in is worth at least half a class level", "[quest_class_xp]")
{
	const proto::ClassEntry entry = makeLinearCurveClass(10);

	// The bare fraction of the level-1 bar (100) is less than a single creature kill grants the
	// same class, so the floor of half the class's current level bar applies instead.
	REQUIRE(ScaleQuestClassXp(1000, 1, 10, entry) == 200);

	// Class level 5 of a level-10 quest: half of the level-5 bar (2000) beats the fraction (500).
	REQUIRE(ScaleQuestClassXp(1000, 5, 10, entry) == 1000);

	// The floor never hands out more than the designer-set reward.
	REQUIRE(ScaleQuestClassXp(150, 1, 10, entry) == 150);

	// ...and never more than the class would get at the quest's own level.
	REQUIRE(ScaleQuestClassXp(1000, 1, 10, entry) <= ScaleQuestClassXp(1000, 10, 10, entry));
}

TEST_CASE("ScaleQuestClassXp - scaled reward never drops to zero", "[quest_class_xp]")
{
	// A reward below the floor is passed through whole - and never scaled away to nothing.
	const proto::ClassEntry entry = makeLinearCurveClass(10);
	REQUIRE(ScaleQuestClassXp(3, 1, 10, entry) == 3);

	// A class curve without an XP bar at the current level has no floor to fall back on, but a
	// non-zero reward still grants at least a single point.
	proto::ClassEntry barlessEntry;
	barlessEntry.add_classlevels()->set_xptonextlevel(0);
	barlessEntry.add_classlevels()->set_xptonextlevel(4000);
	REQUIRE(ScaleQuestClassXp(3, 1, 2, barlessEntry) == 1);
}

TEST_CASE("ScaleQuestClassXp - over-leveled class within grace window grants full XP", "[quest_class_xp]")
{
	const proto::ClassEntry entry = makeLinearCurveClass(20);
	REQUIRE(ScaleQuestClassXp(1000, 15, 10, entry) == 1000);
}

TEST_CASE("ScaleQuestClassXp - over-leveled class beyond grace window steps down like character XP", "[quest_class_xp]")
{
	const proto::ClassEntry entry = makeLinearCurveClass(20);
	REQUIRE(ScaleQuestClassXp(1000, 16, 10, entry) == 800);
	REQUIRE(ScaleQuestClassXp(1000, 17, 10, entry) == 600);
	REQUIRE(ScaleQuestClassXp(1000, 18, 10, entry) == 400);
	REQUIRE(ScaleQuestClassXp(1000, 19, 10, entry) == 200);
	REQUIRE(ScaleQuestClassXp(1000, 20, 10, entry) == 100);
}

TEST_CASE("ScaleQuestClassXp - quest without a level grants full XP", "[quest_class_xp]")
{
	const proto::ClassEntry entry = makeLinearCurveClass(10);
	REQUIRE(ScaleQuestClassXp(1000, 1, 0, entry) == 1000);
}

TEST_CASE("ScaleQuestClassXp - quest level beyond the class curve is clamped to the class cap", "[quest_class_xp]")
{
	const proto::ClassEntry entry = makeLinearCurveClass(10);

	// A level-40 quest cannot demand more than the highest configured class level (10).
	REQUIRE(ScaleQuestClassXp(1000, 1, 40, entry) == ScaleQuestClassXp(1000, 1, 10, entry));
}

TEST_CASE("ScaleQuestClassXp - class without a level curve passes the reward through", "[quest_class_xp]")
{
	const proto::ClassEntry entry;
	REQUIRE(ScaleQuestClassXp(1000, 1, 10, entry) == 1000);
}

TEST_CASE("ScaleQuestClassXp - zero reward stays zero", "[quest_class_xp]")
{
	const proto::ClassEntry entry = makeLinearCurveClass(10);
	REQUIRE(ScaleQuestClassXp(0, 1, 10, entry) == 0);
}
