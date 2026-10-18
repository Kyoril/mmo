// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// A unit spawn point may list several unit entries with weights (a bandit camp spawning mostly
// bandits and sometimes a bandit mage). Every spawn and respawn rolls one of them; without a
// usable list the spawn point keeps spawning its single unitentry.

#include "catch.hpp"

#include "game_server/world/creature_spawner.h"
#include "shared/proto_data/maps.pb.h"

#include <map>

using namespace mmo;

namespace
{
	proto::UnitSpawnEntry MakeSpawn(const uint32 unitEntry)
	{
		proto::UnitSpawnEntry spawn;
		spawn.set_positionx(0.0f);
		spawn.set_positiony(0.0f);
		spawn.set_positionz(0.0f);
		spawn.set_unitentry(unitEntry);
		return spawn;
	}

	void AddCandidate(proto::UnitSpawnEntry& spawn, const uint32 unitEntry, const uint32 weight)
	{
		auto* candidate = spawn.add_unitentries();
		candidate->set_unitentry(unitEntry);
		candidate->set_weight(weight);
	}

	std::map<uint32, int> CountRolls(const proto::UnitSpawnEntry& spawn, const int rolls)
	{
		RandomnessGenerator generator(1234);
		std::map<uint32, int> counts;
		for (int i = 0; i < rolls; ++i)
		{
			++counts[CreatureSpawner::RollUnitEntryId(spawn, generator)];
		}
		return counts;
	}
}

TEST_CASE("A spawn without unitentries always spawns its unitentry", "[creature][spawn]")
{
	const auto counts = CountRolls(MakeSpawn(7), 200);
	REQUIRE(counts.size() == 1);
	CHECK(counts.at(7) == 200);
}

TEST_CASE("A weighted entry's weight defaults to 1", "[creature][spawn]")
{
	proto::UnitSpawnWeightedEntry candidate;
	CHECK(candidate.weight() == 1);
}

TEST_CASE("A single weighted entry replaces unitentry", "[creature][spawn]")
{
	auto spawn = MakeSpawn(7);
	AddCandidate(spawn, 8, 5);

	const auto counts = CountRolls(spawn, 200);
	REQUIRE(counts.size() == 1);
	CHECK(counts.at(8) == 200);
}

TEST_CASE("Weighted entries are rolled in proportion to their weight", "[creature][spawn]")
{
	auto spawn = MakeSpawn(10);
	AddCandidate(spawn, 10, 3);	// bandit
	AddCandidate(spawn, 11, 1);	// bandit mage

	constexpr int rolls = 20000;
	const auto counts = CountRolls(spawn, rolls);
	REQUIRE(counts.size() == 2);
	CHECK(counts.at(10) + counts.at(11) == rolls);

	// Expected 75% / 25%, allow two percentage points of noise.
	CHECK(counts.at(11) > rolls * 23 / 100);
	CHECK(counts.at(11) < rolls * 27 / 100);
}

TEST_CASE("Equal weights roll uniformly", "[creature][spawn]")
{
	auto spawn = MakeSpawn(1);
	AddCandidate(spawn, 1, 2);
	AddCandidate(spawn, 2, 2);
	AddCandidate(spawn, 3, 2);

	constexpr int rolls = 30000;
	const auto counts = CountRolls(spawn, rolls);
	REQUIRE(counts.size() == 3);
	for (const auto& [entry, count] : counts)
	{
		INFO("unit entry " << entry);
		CHECK(count > rolls * 30 / 100);
		CHECK(count < rolls * 36 / 100);
	}
}

TEST_CASE("Weight 0 entries are never rolled", "[creature][spawn]")
{
	auto spawn = MakeSpawn(1);
	AddCandidate(spawn, 1, 0);
	AddCandidate(spawn, 2, 1);
	AddCandidate(spawn, 3, 0);

	const auto counts = CountRolls(spawn, 500);
	REQUIRE(counts.size() == 1);
	CHECK(counts.at(2) == 500);
}

TEST_CASE("A list with only weight 0 entries falls back to unitentry", "[creature][spawn]")
{
	auto spawn = MakeSpawn(9);
	AddCandidate(spawn, 1, 0);
	AddCandidate(spawn, 2, 0);

	const auto counts = CountRolls(spawn, 100);
	REQUIRE(counts.size() == 1);
	CHECK(counts.at(9) == 100);
}

TEST_CASE("Huge weights do not overflow the roll", "[creature][spawn]")
{
	auto spawn = MakeSpawn(1);
	AddCandidate(spawn, 1, 0xFFFFFFFFu);
	AddCandidate(spawn, 2, 0xFFFFFFFFu);

	const auto counts = CountRolls(spawn, 2000);
	REQUIRE(counts.size() == 2);
	CHECK(counts.at(1) > 800);
	CHECK(counts.at(2) > 800);
}
