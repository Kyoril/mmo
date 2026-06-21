// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_server/loot_instance.h"
#include "game_server/objects/game_player_s.h"
#include "game_server/condition_mgr.h"
#include "game/group.h"
#include "game/object_type_id.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "shared/proto_data/loot_entry.pb.h"
#include "asio/io_service.hpp"

#include <memory>
#include <vector>

using namespace mmo;

namespace
{
	/// Helper to create a proto::LootEntry with a given number of 100%-drop-chance item groups.
	/// Each group contains one definition using item id=1, dropchance=100%, count=1.
	/// @param numGroups Number of groups (= guaranteed items) to create.
	proto::LootEntry MakeLootEntry(int numGroups)
	{
		proto::LootEntry entry;
		for (int i = 0; i < numGroups; ++i)
		{
			auto* group = entry.add_groups();
			auto* def = group->add_definitions();
			def->set_item(1);
			def->set_dropchance(100.0f);
			def->set_mincount(1);
			def->set_maxcount(1);
		}
		return entry;
	}

	/// Helper to create a GamePlayerS with a specific GUID for use in loot tests.
	/// @param project The game project (empty is fine for loot tests).
	/// @param timerQueue A timer queue instance.
	/// @param guid The GUID to assign to the player.
	/// @returns A shared_ptr to the initialized player.
	std::shared_ptr<GamePlayerS> MakePlayer(
		const proto::Project& project,
		TimerQueue& timerQueue,
		uint64 guid)
	{
		auto player = std::make_shared<GamePlayerS>(project, timerQueue);
		player->Initialize();
		player->Set<uint64>(object_fields::Guid, guid, false);
		return player;
	}
}

TEST_CASE("LootInstance MasterLoot enforcement", "[loot_instance]")
{
	asio::io_service ioService;
	TimerQueue timerQueue(ioService);
	proto::Project project;

	// Add item id=1 so TakeItem can find it in the manager
	auto* itemEntry = project.items.add(1);
	itemEntry->set_id(1);
	itemEntry->set_maxstack(20);

	ConditionMgr conditionMgr(project.conditions);

	const uint64 masterGuid = 1001;
	const uint64 nonMasterGuid = 1002;

	proto::LootEntry entry = MakeLootEntry(1);

	// One item, MasterLoot mode — lootMasterGuid = masterGuid
	std::vector<std::weak_ptr<GamePlayerS>> recipients;
	LootInstance loot(
		project.items,
		conditionMgr,
		42ULL,
		&entry,
		0, 0,
		recipients,
		loot_method::MasterLoot,
		masterGuid);

	SECTION("LOOT-01: Non-master player is rejected")
	{
		REQUIRE(loot.TakeItem(0, nonMasterGuid) == false);
	}

	SECTION("LOOT-02: Loot master player is accepted")
	{
		REQUIRE(loot.TakeItem(0, masterGuid) == true);
	}
}

TEST_CASE("LootInstance RoundRobin enforcement", "[loot_instance]")
{
	asio::io_service ioService;
	TimerQueue timerQueue(ioService);
	proto::Project project;

	// Add item id=1
	auto* itemEntry = project.items.add(1);
	itemEntry->set_id(1);
	itemEntry->set_maxstack(20);

	ConditionMgr conditionMgr(project.conditions);

	const uint64 guidA = 1001;
	const uint64 guidB = 1002;

	// Two recipients: A (slot 0 assigned), B (slot 1 assigned), A again (slot 2)
	auto playerA = MakePlayer(project, timerQueue, guidA);
	auto playerB = MakePlayer(project, timerQueue, guidB);
	std::vector<std::weak_ptr<GamePlayerS>> recipients = { playerA, playerB };

	proto::LootEntry entry = MakeLootEntry(3);

	LootInstance loot(
		project.items,
		conditionMgr,
		43ULL,
		&entry,
		0, 0,
		recipients,
		loot_method::RoundRobin,
		0);
	loot.SetRoundRobinLooter(guidA);

	SECTION("LOOT-03: Non-designated player cannot take round-robin loot")
	{
		REQUIRE(loot.TakeItem(0, guidB) == false);
	}

	SECTION("LOOT-04: Designated player can take round-robin loot")
	{
		REQUIRE(loot.TakeItem(0, guidA) == true);
	}

	SECTION("LOOT-05: Designated player can take multiple low-quality slots on this corpse")
	{
		REQUIRE(loot.TakeItem(0, guidA) == true);
		REQUIRE(loot.TakeItem(1, guidA) == true);
		REQUIRE(loot.TakeItem(2, guidA) == true);
	}
}

TEST_CASE("LootInstance FreeForAll enforcement", "[loot_instance]")
{
	asio::io_service ioService;
	TimerQueue timerQueue(ioService);
	proto::Project project;

	// Add item id=1
	auto* itemEntry = project.items.add(1);
	itemEntry->set_id(1);
	itemEntry->set_maxstack(20);

	ConditionMgr conditionMgr(project.conditions);

	proto::LootEntry entry = MakeLootEntry(1);
	std::vector<std::weak_ptr<GamePlayerS>> recipients;

	LootInstance loot(
		project.items,
		conditionMgr,
		44ULL,
		&entry,
		0, 0,
		recipients,
		loot_method::FreeForAll,
		0);

	SECTION("LOOT-06: Any player can take any item in FreeForAll mode")
	{
		const uint64 anyGuid = 9999;
		REQUIRE(loot.TakeItem(0, anyGuid) == true);
	}
}

TEST_CASE("LootInstance strategy-aware slot visibility", "[loot_instance]")
{
	asio::io_service ioService;
	TimerQueue timerQueue(ioService);
	proto::Project project;

	auto* itemEntry = project.items.add(1);
	itemEntry->set_id(1);
	itemEntry->set_maxstack(20);

	ConditionMgr conditionMgr(project.conditions);

	const uint64 guidA = 1001;
	const uint64 guidB = 1002;
	auto playerA = MakePlayer(project, timerQueue, guidA);
	auto playerB = MakePlayer(project, timerQueue, guidB);
	std::vector<std::weak_ptr<GamePlayerS>> recipients = { playerA, playerB };

	SECTION("LOOT-07: MasterLoot only exposes lootable slots to the master")
	{
		proto::LootEntry entry = MakeLootEntry(1);
		LootInstance loot(
			project.items,
			conditionMgr,
			45ULL,
			&entry,
			0, 0,
			recipients,
			loot_method::MasterLoot,
			guidA);

		REQUIRE(loot.CanLootItem(0, guidA) == true);
		REQUIRE(loot.CanLootItem(0, guidB) == false);
		REQUIRE(loot.ContainsLootFor(guidA) == true);
		REQUIRE(loot.ContainsLootFor(guidB) == false);
	}

	SECTION("LOOT-08: RoundRobin exposes only the designated looter's slots")
	{
		proto::LootEntry entry = MakeLootEntry(2);
		LootInstance loot(
			project.items,
			conditionMgr,
			46ULL,
			&entry,
			0, 0,
			recipients,
			loot_method::RoundRobin,
			0);
		loot.SetRoundRobinLooter(guidA);

		REQUIRE(loot.CanLootItem(0, guidA) == true);
		REQUIRE(loot.CanLootItem(0, guidB) == false);
		REQUIRE(loot.CanLootItem(1, guidA) == true);
		REQUIRE(loot.CanLootItem(1, guidB) == false);
		REQUIRE(loot.ContainsLootFor(guidA) == true);
		REQUIRE(loot.ContainsLootFor(guidB) == false);
	}

	SECTION("LOOT-09: GroupLoot marks threshold items as rolling until all votes are submitted")
	{
		itemEntry->set_quality(item_quality::Uncommon);

		proto::LootEntry entry = MakeLootEntry(1);
		LootInstance loot(
			project.items,
			conditionMgr,
			47ULL,
			&entry,
			0, 0,
			recipients,
			loot_method::GroupLoot,
			0);
		loot.SetRoundRobinLooter(guidA);
		loot.SetupGroupRollItems({ guidA, guidB });

		REQUIRE(loot.ContainsLootFor(guidA) == true);
		REQUIRE(loot.ContainsLootFor(guidB) == true);
		REQUIRE(loot.CanLootItem(0, guidA) == false);
		REQUIRE(loot.CanLootItem(0, guidB) == false);
		REQUIRE(loot.GetRollDataMap().contains(0));

		REQUIRE(loot.SubmitRollVote(0, guidA, roll_vote::Pass, "PlayerA") == true);
		REQUIRE(loot.GetRollDataMap().contains(0));
		REQUIRE(loot.SubmitRollVote(0, guidB, roll_vote::Pass, "PlayerB") == true);
		REQUIRE(loot.GetRollDataMap().contains(0) == false);

		// All passed - item becomes free-for-all, not removed
		REQUIRE(loot.IsEmpty() == false);
		REQUIRE(loot.CanLootItem(0, guidA) == true);
		REQUIRE(loot.CanLootItem(0, guidB) == true);
	}
}

TEST_CASE("LootInstance combines multiple loot tables", "[loot_instance]")
{
	asio::io_service ioService;
	TimerQueue timerQueue(ioService);
	proto::Project project;

	auto* itemEntry = project.items.add(1);
	itemEntry->set_id(1);
	itemEntry->set_maxstack(20);

	ConditionMgr conditionMgr(project.conditions);

	std::vector<std::weak_ptr<GamePlayerS>> recipients;

	SECTION("LOOT-10: Items from every assigned loot table are generated")
	{
		// Two modular loot tables: one with two guaranteed items, one with three.
		proto::LootEntry tableA = MakeLootEntry(2);
		proto::LootEntry tableB = MakeLootEntry(3);

		const std::vector<const proto::LootEntry*> entries{ &tableA, &tableB };
		LootInstance loot(project.items, conditionMgr, 42ULL, entries, recipients);

		// All five guaranteed items (2 + 3) should be present.
		REQUIRE(loot.GetItemCount() == 5);
	}

	SECTION("LOOT-11: Gold of every assigned loot table is summed up")
	{
		proto::LootEntry tableA = MakeLootEntry(0);
		tableA.set_minmoney(100);
		tableA.set_maxmoney(100);

		proto::LootEntry tableB = MakeLootEntry(0);
		tableB.set_minmoney(50);
		tableB.set_maxmoney(50);

		const std::vector<const proto::LootEntry*> entries{ &tableA, &tableB };
		LootInstance loot(project.items, conditionMgr, 42ULL, entries, recipients);

		REQUIRE(loot.GetGold() == 150);
	}

	SECTION("LOOT-12: Null loot table entries are ignored")
	{
		proto::LootEntry tableA = MakeLootEntry(1);

		const std::vector<const proto::LootEntry*> entries{ nullptr, &tableA, nullptr };
		LootInstance loot(project.items, conditionMgr, 42ULL, entries, recipients);

		REQUIRE(loot.GetItemCount() == 1);
	}
}
