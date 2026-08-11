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
	REQUIRE(ScaleQuestClassXp(1000, 1, 10, entry) == 100);

	// Class level 5: 2000 / 4000 of the reward.
	REQUIRE(ScaleQuestClassXp(1000, 5, 10, entry) == 500);
}

TEST_CASE("ScaleQuestClassXp - scaled reward never drops to zero", "[quest_class_xp]")
{
	const proto::ClassEntry entry = makeLinearCurveClass(10);
	REQUIRE(ScaleQuestClassXp(3, 1, 10, entry) == 1);
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
