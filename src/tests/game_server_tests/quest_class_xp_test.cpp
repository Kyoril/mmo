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

TEST_CASE("ScaleQuestClassXp - under-leveled class within the grace window grants full XP", "[quest_class_xp]")
{
	const proto::ClassEntry entry = makeLinearCurveClass(40);

	// The reported case: a level-6 quest worth 800 class XP turned in by a level-1 class.
	REQUIRE(ScaleQuestClassXp(800, 1, 6, entry) == 800);

	// Exactly five levels below the quest level is still inside the grace window.
	REQUIRE(ScaleQuestClassXp(3000, 5, 10, entry) == 3000);
}

TEST_CASE("ScaleQuestClassXp - a moderately higher level quest is only mildly reduced", "[quest_class_xp]")
{
	const proto::ClassEntry entry = makeLinearCurveClass(40);

	// A level-10 quest at class level 3 is seven levels below the quest level: two levels past the
	// grace window, so two thirtieths of the reward are lost - not the bulk of it.
	REQUIRE(ScaleQuestClassXp(900, 3, 10, entry) == 840);

	// Nine levels below: four thirtieths lost.
	REQUIRE(ScaleQuestClassXp(3000, 1, 10, entry) == 2600);
}

TEST_CASE("ScaleQuestClassXp - a far higher level quest bottoms out at a third of the reward", "[quest_class_xp]")
{
	const proto::ClassEntry entry = makeLinearCurveClass(40);

	// Twenty-five levels below the quest level is the floor...
	REQUIRE(ScaleQuestClassXp(3000, 5, 30, entry) == 1000);

	// ...and banking level-40 quests for a level-10 class does not go below it.
	REQUIRE(ScaleQuestClassXp(3000, 10, 40, entry) == 1000);
	REQUIRE(ScaleQuestClassXp(3000, 1, 40, entry) == 1000);
}

TEST_CASE("ScaleQuestClassXp - scaled reward never drops to zero", "[quest_class_xp]")
{
	const proto::ClassEntry entry = makeLinearCurveClass(40);

	// A reward small enough that a third of it rounds away still grants a single point.
	REQUIRE(ScaleQuestClassXp(1, 1, 40, entry) == 1);

	// A reward inside the grace window is passed through whole.
	REQUIRE(ScaleQuestClassXp(3, 1, 5, entry) == 3);
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
