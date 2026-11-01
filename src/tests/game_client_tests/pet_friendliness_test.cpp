// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game/guid_utils.h"
#include "game/owner_chain.h"
#include "bot_core/bot_unit.h"

#include <map>

using namespace mmo;

namespace
{
	/// A unit as the client sees it, reduced to what the owner-aware reaction rule needs.
	struct FakeUnit
	{
		uint64 guid = 0;
		uint64 owner = 0;
		/// The faction group: units of the same group are friendly, units of different groups hostile,
		/// group 0 is neutral to everyone.
		uint32 faction = 0;
	};

	/// The client's object list.
	struct FakeWorld
	{
		std::map<uint64, FakeUnit> units;

		FakeUnit& Add(const uint64 guid, const uint32 faction, const uint64 owner = 0)
		{
			FakeUnit& unit = units[guid];
			unit.guid = guid;
			unit.faction = faction;
			unit.owner = owner;
			return unit;
		}

		const FakeUnit* FindOwner(const FakeUnit& unit) const
		{
			if (unit.owner == 0)
			{
				return nullptr;
			}

			const auto it = units.find(unit.owner);
			return it == units.end() ? nullptr : &it->second;
		}

		bool IsFriendly(const FakeUnit& a, const FakeUnit& b) const
		{
			return owner_chain::IsFriendly(a, b,
				[this](const FakeUnit& unit) { return FindOwner(unit); },
				[](const FakeUnit& x, const FakeUnit& y) { return x.faction != 0 && x.faction == y.faction; });
		}

		bool IsHostile(const FakeUnit& a, const FakeUnit& b) const
		{
			return owner_chain::IsHostile(a, b,
				[this](const FakeUnit& unit) { return FindOwner(unit); },
				[](const FakeUnit& x, const FakeUnit& y) { return x.faction != 0 && y.faction != 0 && x.faction != y.faction; });
		}
	};

	constexpr uint32 PlayerFaction = 1;
	constexpr uint32 MonsterFaction = 2;
}

TEST_CASE("A pet with a hostile faction owned by the player is friendly to the player", "[pets]")
{
	FakeWorld world;
	const FakeUnit& player = world.Add(1, PlayerFaction);
	const FakeUnit& pet = world.Add(2, MonsterFaction, player.guid);

	CHECK(world.IsFriendly(pet, player));
	CHECK(world.IsFriendly(player, pet));
	CHECK_FALSE(world.IsHostile(pet, player));
	CHECK_FALSE(world.IsHostile(player, pet));
}

TEST_CASE("A unit with a friendly faction owned by a hostile creature is hostile", "[pets]")
{
	FakeWorld world;
	const FakeUnit& player = world.Add(1, PlayerFaction);
	const FakeUnit& boss = world.Add(2, MonsterFaction);
	const FakeUnit& add = world.Add(3, PlayerFaction, boss.guid);

	CHECK(world.IsHostile(add, player));
	CHECK(world.IsHostile(player, add));
	CHECK_FALSE(world.IsFriendly(add, player));
}

TEST_CASE("Pets of the same top owner are friendly to each other", "[pets]")
{
	FakeWorld world;
	const FakeUnit& player = world.Add(1, PlayerFaction);
	const FakeUnit& wolf = world.Add(2, MonsterFaction, player.guid);
	const FakeUnit& totem = world.Add(3, 0, wolf.guid);

	CHECK(world.IsFriendly(wolf, totem));
	CHECK(world.IsFriendly(totem, player));
	CHECK_FALSE(world.IsHostile(totem, wolf));
}

TEST_CASE("A pet acts with its top owner's faction towards strangers", "[pets]")
{
	FakeWorld world;
	const FakeUnit& player = world.Add(1, PlayerFaction);
	const FakeUnit& pet = world.Add(2, MonsterFaction, player.guid);
	const FakeUnit& wildWolf = world.Add(3, MonsterFaction);
	const FakeUnit& otherPlayer = world.Add(4, PlayerFaction);

	CHECK(world.IsHostile(pet, wildWolf));
	CHECK_FALSE(world.IsFriendly(pet, wildWolf));
	CHECK(world.IsFriendly(pet, otherPlayer));
}

TEST_CASE("An owner that is out of sight leaves the pet with its own faction", "[pets]")
{
	FakeWorld world;
	const FakeUnit& player = world.Add(1, PlayerFaction);
	const FakeUnit& strayPet = world.Add(2, MonsterFaction, 99);

	CHECK(world.IsHostile(strayPet, player));
}

TEST_CASE("Owner chains are followed at most MaxDepth links", "[pets]")
{
	FakeWorld world;
	world.Add(1, PlayerFaction);
	// 2 is owned by 1, 3 by 2, ... 6 by 5: six units, five links from 6 to 1.
	for (uint64 guid = 2; guid <= 6; ++guid)
	{
		world.Add(guid, MonsterFaction, guid - 1);
	}

	const auto findOwner = [&world](const FakeUnit& unit) { return world.FindOwner(unit); };

	// Four links are followed: 5 -> 4 -> 3 -> 2 -> 1.
	CHECK(owner_chain::ResolveTopOwner(world.units[5], findOwner).guid == 1);
	// From 6 the walk stops after four links, at 2.
	CHECK(owner_chain::ResolveTopOwner(world.units[6], findOwner).guid == 2);
}

TEST_CASE("Looping owner chains terminate", "[pets]")
{
	FakeWorld world;
	world.Add(1, MonsterFaction, 2);
	world.Add(2, MonsterFaction, 1);
	world.Add(3, MonsterFaction, 3);

	const auto findOwner = [&world](const FakeUnit& unit) { return world.FindOwner(unit); };

	const FakeUnit& top = owner_chain::ResolveTopOwner(world.units[1], findOwner);
	CHECK((top.guid == 1 || top.guid == 2));
	CHECK(owner_chain::ResolveTopOwner(world.units[3], findOwner).guid == 3);

	// The reaction checks terminate as well.
	CHECK(world.IsFriendly(world.units[3], world.units[3]));
}

TEST_CASE("Bot units: a player's pet is friendly to its owner and to other players", "[pets]")
{
	const uint64 ownerGuid = CreateRealmGUID(1, 0, GuidType::Player);
	const uint64 otherPlayerGuid = CreateRealmGUID(2, 0, GuidType::Player);
	const uint64 petGuid = CreateEntryGUID(1, 5, GuidType::Pet);

	BotUnit owner(ownerGuid, ObjectTypeId::Player);
	BotUnit otherPlayer(otherPlayerGuid, ObjectTypeId::Player);
	BotUnit pet(petGuid, ObjectTypeId::Unit);
	pet.SetOwnerGuid(ownerGuid);

	CHECK(pet.IsFriendlyTo(owner));
	CHECK(owner.IsFriendlyTo(pet));
	CHECK_FALSE(pet.IsHostileTo(owner));
	CHECK_FALSE(owner.IsHostileTo(pet));
	CHECK_FALSE(pet.IsHostileTo(otherPlayer));
	CHECK_FALSE(otherPlayer.IsHostileTo(pet));
}

TEST_CASE("Bot units: a player's pet is hostile to a wild creature", "[pets]")
{
	const uint64 ownerGuid = CreateRealmGUID(1, 0, GuidType::Player);
	const uint64 petGuid = CreateEntryGUID(1, 5, GuidType::Pet);
	const uint64 wolfGuid = CreateEntryGUID(9, 7, GuidType::Unit);

	BotUnit pet(petGuid, ObjectTypeId::Unit);
	pet.SetOwnerGuid(ownerGuid);
	BotUnit wolf(wolfGuid, ObjectTypeId::Unit);

	CHECK(pet.IsHostileTo(wolf));
	CHECK(wolf.IsHostileTo(pet));
}

TEST_CASE("Bot units: pets of the same owner are friendly to each other", "[pets]")
{
	const uint64 ownerGuid = CreateRealmGUID(1, 0, GuidType::Player);
	BotUnit first(CreateEntryGUID(1, 5, GuidType::Pet), ObjectTypeId::Unit);
	BotUnit second(CreateEntryGUID(2, 6, GuidType::Unit), ObjectTypeId::Unit);
	first.SetOwnerGuid(ownerGuid);
	second.SetOwnerGuid(ownerGuid);

	CHECK(first.IsFriendlyTo(second));
	CHECK_FALSE(first.IsHostileTo(second));
}
