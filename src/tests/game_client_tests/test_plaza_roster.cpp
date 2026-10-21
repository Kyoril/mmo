// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_client/plaza_roster.h"

using namespace mmo;

TEST_CASE("No characters leaves every spot empty", "[plaza]")
{
	const auto a = AssignPlaza(0, -1);
	CHECK(a.frontCharacter == -1);
	for (const int32 c : a.spotCharacter)
	{
		CHECK(c == -1);
	}
}

TEST_CASE("A selected plaza character leaves their spot for the front", "[plaza]")
{
	const auto a = AssignPlaza(7, 2);
	CHECK(a.frontCharacter == 2);
	CHECK_FALSE(a.frontIsVisitor);
	CHECK(a.spotCharacter == std::array<int32, 5>{ 0, 1, -1, 3, 4 });
}

TEST_CASE("A visitor takes the front and all five stay at their spots", "[plaza]")
{
	const auto a = AssignPlaza(7, 6);
	CHECK(a.frontCharacter == 6);
	CHECK(a.frontIsVisitor);
	CHECK(a.spotCharacter == std::array<int32, 5>{ 0, 1, 2, 3, 4 });
}

TEST_CASE("Initial selection falls back to the first character", "[plaza]")
{
	CHECK(ResolveInitialSelection({ 10, 20, 30 }, 20) == 1);
	CHECK(ResolveInitialSelection({ 10, 20, 30 }, 99) == 0);
	CHECK(ResolveInitialSelection({}, 20) == -1);
}

TEST_CASE("Roster entries move to the drop position", "[plaza]")
{
	std::vector<uint64> order{ 1, 2, 3, 4, 5, 6 };
	MoveRosterEntry(order, 5, 0);
	CHECK(order == std::vector<uint64>{ 6, 1, 2, 3, 4, 5 });
	MoveRosterEntry(order, 0, 5);
	CHECK(order == std::vector<uint64>{ 1, 2, 3, 4, 5, 6 });
}

TEST_CASE("Last character is remembered per realm", "[plaza]")
{
	String v = SetLastCharacterForRealm("", 1, 42);
	v = SetLastCharacterForRealm(v, 2, 7);
	v = SetLastCharacterForRealm(v, 1, 43);
	CHECK(GetLastCharacterForRealm(v, 1) == 43);
	CHECK(GetLastCharacterForRealm(v, 2) == 7);
	CHECK(GetLastCharacterForRealm(v, 3) == 0);
	CHECK(GetLastCharacterForRealm("garbage;1:x", 1) == 0);
	CHECK(v == "1:43;2:7");
}

TEST_CASE("Arrival depends on readiness and origin", "[plaza]")
{
	CHECK(DecideGlueArrival(true, false) == GlueArrival::FlyIn);
	CHECK(DecideGlueArrival(false, false) == GlueArrival::FadeWhenReady);
	CHECK(DecideGlueArrival(true, true) == GlueArrival::Snap);
	CHECK(DecideGlueArrival(false, true) == GlueArrival::FadeWhenReady);
}

TEST_CASE("Plaza appearance of identical views is equal", "[plaza]")
{
	CharacterView a(7, "Alda", 5, 0, 1, 1, 1, 0, false, 42);
	a.GetConfiguration().chosenOptionPerGroup[3] = 9;
	a.GetConfiguration().scalarValues[4] = 1.25f;
	a.SetEquipmentDisplayId(2, 100);

	const CharacterView b = a;
	CHECK(MakePlazaAppearance(a) == MakePlazaAppearance(b));
}

TEST_CASE("Plaza appearance ignores name, level and zone", "[plaza]")
{
	const CharacterView a(7, "Alda", 5, 0, 1, 1, 1, 0, false, 42);
	const CharacterView b(7, "Other", 9, 1, 12, 1, 3, 0, true, 42);
	CHECK(MakePlazaAppearance(a) == MakePlazaAppearance(b));
}

TEST_CASE("Plaza appearance differs on model, customization or gear", "[plaza]")
{
	const CharacterView base(7, "Alda", 5, 0, 1, 1, 1, 0, false, 42);
	const PlazaAppearance baseAppearance = MakePlazaAppearance(base);

	SECTION("display id")
	{
		const CharacterView other(7, "Alda", 5, 0, 1, 1, 1, 0, false, 43);
		CHECK(MakePlazaAppearance(other) != baseAppearance);
	}

	SECTION("gender")
	{
		const CharacterView other(7, "Alda", 5, 0, 1, 1, 1, 1, false, 42);
		CHECK(MakePlazaAppearance(other) != baseAppearance);
	}

	SECTION("customization option")
	{
		CharacterView other = base;
		other.GetConfiguration().chosenOptionPerGroup[3] = 9;
		CHECK(MakePlazaAppearance(other) != baseAppearance);
	}

	SECTION("scalar value")
	{
		CharacterView other = base;
		other.GetConfiguration().scalarValues[4] = 0.9f;
		CHECK(MakePlazaAppearance(other) != baseAppearance);
	}

	SECTION("equipment")
	{
		CharacterView other = base;
		other.SetEquipmentDisplayId(5, 77);
		CHECK(MakePlazaAppearance(other) != baseAppearance);
	}
}
