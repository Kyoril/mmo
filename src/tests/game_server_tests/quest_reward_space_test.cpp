// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Turning in a quest is all or nothing: a turn-in that does not fit must leave the quest
// completable and hand out nothing. Checking each reward entry on its own was not enough,
// because the entries compete for the same empty slots - a two-item reward with room for only
// one used to create the first item, fail on the second and return without completing the
// quest, so the turn-in could be repeated to farm the part that did fit.

#include "catch.hpp"

#include "game_server/condition_mgr.h"
#include "game_server/inventory.h"
#include "game_server/inventory_types.h"
#include "game_server/objects/game_item_s.h"
#include "game_server/objects/game_player_s.h"
#include "game_server/trigger_handler.h"
#include "game_server/world/universe.h"
#include "game_server/world/world_instance.h"
#include "game_server/world/world_instance_manager.h"
#include "game/quest.h"
#include "test_unit_factory.h"
#include "base/id_generator.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "asio/io_service.hpp"

#include <memory>
#include <vector>

using namespace mmo;

namespace
{
	constexpr uint32 FillerItemId = 900;
	constexpr uint32 FirstRewardItemId = 901;
	constexpr uint32 SecondRewardItemId = 902;
	constexpr uint32 TestQuestId = 500;

	struct NullTriggerHandler final : ITriggerHandler
	{
		void ExecuteTrigger(const proto::TriggerEntry&, TriggerContext, uint32, bool) override {}
	};

	/// Records the inventory errors the player is told about, so a refused turn-in can be told
	/// apart from one that silently did nothing.
	class QuestRewardWatcher final : public NetPlayerWatcher
	{
	public:
		void OnQuestKillCredit(const proto::QuestEntry&, uint64, uint32, uint32, uint32) override {}

		void OnQuestItemCredit(const proto::QuestEntry&, uint32, uint32, uint32) override {}

		void OnQuestObjectRequirementMet(uint32) override {}

		void OnQuestDataChanged(uint32, const QuestStatusData&) override {}

		void OnQuestCompleted(uint64, uint32, uint32, uint32) override {}

		void OnItemAdded(uint16, uint16, bool, bool) override {}

		void OnObjectLoot() override {}

		void OnInventoryError(const InventoryChangeFailure error) override { errors.push_back(error); }

		std::vector<InventoryChangeFailure> errors;
	};

	/// A player in a world instance holding a completed quest that rewards two different items.
	/// Inventory::CreateItems needs the world instance for its item ids, so the turn-in can only
	/// be observed with one in place.
	struct QuestRewardFixture
	{
		asio::io_service io;
		TimerQueue timers{ io };
		proto::Project project;
		Universe universe{ io, timers };
		IdGenerator<uint64> objectIds{ 1 };
		NullTriggerHandler triggerHandler;
		std::unique_ptr<ConditionMgr> conditionMgr;
		std::unique_ptr<WorldInstanceManager> manager;
		WorldInstance* world = nullptr;

		proto::ItemEntry* filler = nullptr;
		proto::QuestEntry* quest = nullptr;
		std::shared_ptr<GamePlayerS> player;
		QuestRewardWatcher watcher;

		QuestRewardFixture()
		{
			auto* race = project.races.add(1);
			race->set_name("Human");

			// No map files exist for this directory, which is all this suite needs from the world.
			auto* map = project.maps.add(0);
			map->set_name("Quest Reward Test");
			map->set_directory("Worlds/__quest_reward_test__/__quest_reward_test__");

			filler = AddItem(FillerItemId, "Filler");
			AddItem(FirstRewardItemId, "Worn Dagger");
			AddItem(SecondRewardItemId, "Worn Shield");

			quest = project.quests.add(TestQuestId);
			quest->set_name("Two Trinkets");
			quest->set_minlevel(1);
			AddQuestReward(FirstRewardItemId);
			AddQuestReward(SecondRewardItemId);

			conditionMgr = std::make_unique<ConditionMgr>(project.conditions);
			manager = std::make_unique<WorldInstanceManager>(io, universe, project, objectIds, triggerHandler, *conditionMgr);
			world = &manager->CreateInstance(0);

			player = test::MakeUnit(project, timers);
			player->SetRace(*race);
			player->SetWorldInstance(world);
			player->SetPlayerWatcher(&watcher);
		}

		proto::ItemEntry* AddItem(const uint32 id, const char* name)
		{
			auto* entry = project.items.add(id);
			entry->set_name(name);
			entry->set_maxstack(1);
			return entry;
		}

		void AddQuestReward(const uint32 itemId)
		{
			auto* reward = quest->add_rewarditems();
			reward->set_itemid(itemId);
			reward->set_count(1);
		}

		/// Fills the backpack with filler items, leaving exactly the given number of slots free.
		void FillBackpack(const uint16 freeSlotsToLeave)
		{
			const uint16 end = static_cast<uint16>(player_inventory_pack_slots::End - freeSlotsToLeave);
			for (uint16 slot = player_inventory_pack_slots::Start; slot < end; ++slot)
			{
				auto item = std::make_shared<GameItemS>(project, *filler);
				item->Initialize();
				item->Set<uint64>(object_fields::Guid, CreateEntryGUID(2000 + slot, filler->id(), GuidType::Item));
				player->GetInventory().AddItemToSlot(item, InventorySlot::FromRelative(player_inventory_slots::Bag_0, static_cast<uint8>(slot)).GetAbsolute());
			}
		}
	};
}

TEST_CASE("A quest turn-in that does not fit grants none of its rewards", "[quest_reward]")
{
	QuestRewardFixture fixture;

	// The quest has no requirements, so accepting it completes it right away.
	REQUIRE(fixture.player->AcceptQuest(TestQuestId));
	REQUIRE(fixture.player->GetQuestStatus(TestQuestId) == quest_status::Complete);

	// Two rewards, but only room for one of them.
	fixture.FillBackpack(1);

	REQUIRE_FALSE(fixture.player->RewardQuest(fixture.player->GetGuid(), TestQuestId, 0));

	// The quest is still sitting there completable, so not a single reward may have been handed
	// out - otherwise the turn-in can be repeated to farm the part that did fit.
	CHECK(fixture.player->GetQuestStatus(TestQuestId) == quest_status::Complete);
	CHECK(fixture.player->GetInventory().GetItemCount(FirstRewardItemId) == 0);
	CHECK(fixture.player->GetInventory().GetItemCount(SecondRewardItemId) == 0);

	// The player has to learn why the turn-in was refused.
	REQUIRE_FALSE(fixture.watcher.errors.empty());
	CHECK(fixture.watcher.errors.back() == inventory_change_failure::InventoryFull);
}

TEST_CASE("A quest turn-in with room for every reward still hands them all out", "[quest_reward]")
{
	QuestRewardFixture fixture;

	REQUIRE(fixture.player->AcceptQuest(TestQuestId));
	REQUIRE(fixture.player->GetQuestStatus(TestQuestId) == quest_status::Complete);

	fixture.FillBackpack(2);

	REQUIRE(fixture.player->RewardQuest(fixture.player->GetGuid(), TestQuestId, 0));
	CHECK(fixture.player->GetInventory().GetItemCount(FirstRewardItemId) == 1);
	CHECK(fixture.player->GetInventory().GetItemCount(SecondRewardItemId) == 1);
	CHECK(fixture.watcher.errors.empty());
}
