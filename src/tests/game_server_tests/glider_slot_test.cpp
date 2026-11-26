// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// A glider in the glider slot is shown on the player and is saved with its own slot number.

#include "game_server/inventory.h"
#include "game_server/inventory_repository.h"
#include "game_server/inventory_types.h"
#include "game_server/objects/game_item_s.h"
#include "game_server/objects/game_player_s.h"
#include "game/inventory_fields.h"
#include "game/traversal.h"
#include "base/signal.h"
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

namespace
{
	std::shared_ptr<GameItemS> MakeGlider(proto::Project& project, const uint64 guidCounter)
	{
		auto* entry = project.items.getById(181);
		if (!entry)
		{
			entry = project.items.add(181);
			entry->set_name("Elowen's Prototype");
			entry->set_maxstack(1);
			entry->set_inventorytype(inventory_type::Glider);
		}

		auto item = std::make_shared<GameItemS>(project, *entry);
		item->Initialize();
		item->Set<uint64>(object_fields::Guid, CreateEntryGUID(guidCounter, entry->id(), GuidType::Item));
		return item;
	}

	void AddTraversalSpell(proto::Project& project, const uint32 id)
	{
		auto* spell = project.spells.add(id);
		spell->set_name(id == traversal_spells::Glider ? "Glider" : "Dash");
		spell->add_attributes(0);
		spell->add_attributes(0);
	}
}

TEST_CASE("The glider spell works only with a glider equipped", "[glider_gate]")
{
	asio::io_service io;
	TimerQueue timers(io);
	proto::Project project;
	AddTraversalSpell(project, traversal_spells::Glider);
	AddTraversalSpell(project, traversal_spells::Dash);

	auto player = test::MakeUnit(project, timers);
	player->Set<uint64>(object_fields::Guid, 1);
	player->AddSpell(traversal_spells::Glider);
	CHECK_FALSE(player->HasGliderEquipped());
	CHECK_FALSE(player->CanUseTraversal(traversal_spells::Glider));
	CHECK(player->Get<float>(object_fields::MaxVigor) == Approx(0.0f));

	uint32 unequipped = 0;
	scoped_connection connection{ player->gliderUnequipped.connect([&unequipped]() { ++unequipped; }) };

	auto glider = MakeGlider(project, 7);
	player->GetInventory().AddItemToSlot(glider, GliderAbsoluteSlot);
	CHECK(player->HasGliderEquipped());
	CHECK(player->CanUseTraversal(traversal_spells::Glider));
	CHECK(player->Get<float>(object_fields::MaxVigor) == Approx(100.0f));

	player->GetInventory().RemoveItem(GliderAbsoluteSlot);
	CHECK_FALSE(player->HasGliderEquipped());
	CHECK_FALSE(player->CanUseTraversal(traversal_spells::Glider));
	CHECK(player->Get<float>(object_fields::MaxVigor) == Approx(0.0f));
	CHECK(unequipped == 1);
}

TEST_CASE("Without the glider spell an equipped glider does nothing", "[glider_gate]")
{
	asio::io_service io;
	TimerQueue timers(io);
	proto::Project project;
	AddTraversalSpell(project, traversal_spells::Glider);

	auto player = test::MakeUnit(project, timers);
	player->Set<uint64>(object_fields::Guid, 1);
	player->GetInventory().AddItemToSlot(MakeGlider(project, 8), GliderAbsoluteSlot);
	CHECK(player->HasGliderEquipped());
	CHECK_FALSE(player->CanUseTraversal(traversal_spells::Glider));
}

TEST_CASE("An equipped glider's glide stats set the player's glide settings", "[glider_slot]")
{
	asio::io_service io;
	TimerQueue timers(io);
	proto::Project project;
	auto* entry = project.items.add(182);
	entry->set_name("Swift Test Glider");
	entry->set_maxstack(1);
	entry->set_inventorytype(inventory_type::Glider);
	auto* speed = entry->add_stats();
	speed->set_type(item_stat::GlideSpeed);
	speed->set_value(20);
	auto* lift = entry->add_stats();
	lift->set_type(item_stat::GlideLift);
	lift->set_value(40);

	auto player = test::MakeUnit(project, timers);
	player->Set<uint64>(object_fields::Guid, 1);

	auto glider = std::make_shared<GameItemS>(project, *entry);
	glider->Initialize();
	glider->Set<uint64>(object_fields::Guid, CreateEntryGUID(9, entry->id(), GuidType::Item));
	player->GetInventory().AddItemToSlot(glider, GliderAbsoluteSlot);

	CHECK(player->GetVigor().GetSettings().glideForwardSpeed == Approx(12.0f));
	CHECK(player->GetVigor().GetSettings().glideSinkSpeed == Approx(1.5f));

	player->GetInventory().RemoveItem(GliderAbsoluteSlot);
	CHECK(player->GetVigor().GetSettings().glideForwardSpeed == Approx(10.0f));
	CHECK(player->GetVigor().GetSettings().glideSinkSpeed == Approx(2.5f));
}
