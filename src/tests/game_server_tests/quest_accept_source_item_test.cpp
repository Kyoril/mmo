// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/objects/game_player_s.h"
#include "game_server/objects/game_item_s.h"
#include "game_server/inventory.h"
#include "game_server/inventory_types.h"
#include "game/quest.h"
#include "test_unit_factory.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

#include <vector>

using namespace mmo;

namespace
{
	/// Records everything the player tells its client about, so a test can assert that a refused
	/// quest actually produced a reason for the player.
	class QuestAcceptWatcher final : public NetPlayerWatcher
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

	/// Occupies every backpack slot, leaving the inventory without room for another item.
	void FillBackpack(GamePlayerS& player, const proto::Project& project, const proto::ItemEntry& filler)
	{
		for (uint16 slot = player_inventory_pack_slots::Start; slot < player_inventory_pack_slots::End; ++slot)
		{
			auto item = std::make_shared<GameItemS>(project, filler);
			item->Initialize();
			item->Set<uint64>(object_fields::Guid, CreateEntryGUID(2000 + slot, filler.id(), GuidType::Item));
			player.GetInventory().AddItemToSlot(item, InventorySlot::FromRelative(player_inventory_slots::Bag_0, static_cast<uint8>(slot)).GetAbsolute());
		}
	}
}

TEST_CASE("A quest refused because its source item does not fit tells the player why", "[quest_accept]")
{
	asio::io_service io;
	TimerQueue timers(io);
	proto::Project project;

	auto* race = project.races.add(1);
	race->set_name("Human");

	auto* filler = project.items.add(900);
	filler->set_name("Filler");
	filler->set_maxstack(1);

	auto* sourceItem = project.items.add(901);
	sourceItem->set_name("Practice Sewing Kit");
	sourceItem->set_maxstack(1);
	sourceItem->set_maxcount(1);

	auto* quest = project.quests.add(500);
	quest->set_name("Needle and Thread");
	quest->set_minlevel(1);
	quest->set_srcitemid(sourceItem->id());
	quest->set_srcitemcount(1);

	const auto player = test::MakeUnit(project, timers);
	player->SetRace(*race);

	QuestAcceptWatcher watcher;
	player->SetPlayerWatcher(&watcher);

	// The quest is offered to the player: the quest giver menu and the quest details dialog both
	// go by this status, so the client shows an Accept button.
	REQUIRE(player->GetQuestStatus(quest->id()) == quest_status::Available);

	FillBackpack(*player, project, *filler);

	// The source item cannot be granted, so the quest is refused - but the player has to learn
	// the reason instead of the Accept button silently doing nothing.
	REQUIRE_FALSE(player->AcceptQuest(quest->id()));
	REQUIRE(watcher.errors.size() == 1);
	REQUIRE(watcher.errors.front() == inventory_change_failure::InventoryFull);
}
