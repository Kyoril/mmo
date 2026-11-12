// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_client/character_roster.h"

using namespace mmo;

TEST_CASE("Initial selection falls back to the first character", "[roster]")
{
	CHECK(ResolveInitialSelection({ 10, 20, 30 }, 20) == 1);
	CHECK(ResolveInitialSelection({ 10, 20, 30 }, 99) == 0);
	CHECK(ResolveInitialSelection({}, 20) == -1);
}

TEST_CASE("Roster entries move to the drop position", "[roster]")
{
	std::vector<uint64> order{ 1, 2, 3, 4, 5, 6 };
	MoveRosterEntry(order, 5, 0);
	CHECK(order == std::vector<uint64>{ 6, 1, 2, 3, 4, 5 });
	MoveRosterEntry(order, 0, 5);
	CHECK(order == std::vector<uint64>{ 1, 2, 3, 4, 5, 6 });
}

TEST_CASE("Last character is remembered per realm", "[roster]")
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
