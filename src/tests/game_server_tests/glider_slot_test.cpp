// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// A glider in the glider slot is shown on the player and is saved with its own slot number.

#include "game_server/inventory.h"
#include "game_server/inventory_repository.h"
#include "game_server/inventory_types.h"
#include "game_server/objects/game_item_s.h"
#include "game_server/objects/game_player_s.h"
#include "game/inventory_fields.h"
#include "test_unit_factory.h"
#include "base/timer_queue.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

using namespace mmo;

namespace
{
	constexpr uint16 GliderAbsoluteSlot = 0xFF00 | player_glider_slots::Slot;
}

TEST_CASE("A glider in the glider slot sets the glider fields and is saved in slot 0xFF56", "[glider_slot]")
{
	asio::io_service io;
	TimerQueue timers(io);
	proto::Project project;
	auto* entry = project.items.add(181);
	entry->set_name("Elowen's Prototype");
	entry->set_maxstack(1);
	entry->set_inventorytype(inventory_type::Glider);

	auto player = test::MakeUnit(project, timers);
	Inventory& inventory = player->GetInventory();

	InMemoryInventoryRepository repository;
	inventory.SetRepository(&repository);

	auto item = std::make_shared<GameItemS>(project, *entry);
	item->Initialize();
	item->Set<uint64>(object_fields::Guid, CreateEntryGUID(7, entry->id(), GuidType::Item));
	inventory.AddItemToSlot(item, GliderAbsoluteSlot);

	CHECK(player->Get<uint64>(object_fields::GliderSlot) == item->GetGuid());
	CHECK(player->Get<uint32>(object_fields::VisibleGlider_0) == 181);
	// Nothing leaked into the buyback fields that follow the last regular slot.
	CHECK(player->Get<uint64>(object_fields::BuybackPrice_1) == 0);

	inventory.MarkDirty();
	REQUIRE(inventory.SaveToRepository());
	const auto saved = repository.LoadItems(player->GetGuid());
	REQUIRE(saved.size() == 1);
	CHECK(saved[0].slot == GliderAbsoluteSlot);
	CHECK(saved[0].entry == 181);
}
