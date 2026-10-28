// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game/item.h"
#include "game/object_type_id.h"
#include "game_server/inventory.h"
#include "game_server/inventory_types.h"
#include "game_server/objects/game_item_s.h"
#include "game_server/objects/game_player_s.h"
#include "base/timer_queue.h"
#include "binary_io/memory_source.h"
#include "binary_io/reader.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"
#include "shared/proto_data/project.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

#include <vector>

using namespace mmo;

namespace
{
	constexpr uint32 TestItemId = 4711;
	constexpr uint32 TestSellPrice = 25;

	/// Minimal player plus a single item in the first backpack slot, which is all a vendor sale needs.
	struct BuyBackFixture
	{
		asio::io_service io;
		TimerQueue timers{ io };
		proto::Project project;
		std::shared_ptr<GamePlayerS> player;
		proto::ItemEntry* entry = nullptr;

		BuyBackFixture()
		{
			auto* cls = project.classes.add(1);
			cls->set_powertype(proto::ClassEntry_PowerType_MANA);
			for (uint32 i = 0; i < 2; ++i)
			{
				auto* levelValues = cls->add_levelbasevalues();
				levelValues->set_health(100);
				levelValues->set_mana(100);
				levelValues->set_stamina(10);
				levelValues->set_strength(10);
				levelValues->set_agility(10);
				levelValues->set_intellect(10);
				levelValues->set_spirit(10);
			}

			entry = project.items.add(TestItemId);
			entry->set_name("Test Blade");
			entry->set_maxstack(1);
			entry->set_sellprice(TestSellPrice);

			player = std::make_shared<GamePlayerS>(project, timers);
			player->Initialize();
			player->SetClass(*cls);
			player->SetLevel(1);
		}

		uint16 FirstBackpackSlot() const
		{
			return InventorySlot::FromRelative(player_inventory_slots::Bag_0, player_inventory_pack_slots::Start).GetAbsolute();
		}

		/// Puts a single item into the first backpack slot and returns its guid.
		uint64 PlaceItem()
		{
			auto item = std::make_shared<GameItemS>(project, *entry);
			item->Initialize();
			item->Set<uint64>(object_fields::Guid, CreateEntryGUID(1, TestItemId, GuidType::Item));
			item->Set<uint32>(object_fields::StackCount, 1);

			const uint64 guid = item->GetGuid();
			player->GetInventory().AddItemToSlot(item, FirstBackpackSlot());
			return guid;
		}
	};

	/// Serializes the inventory the way the world node hands it back to the realm and returns the
	/// number of items the realm would store.
	uint16 SerializedItemCount(const Inventory& inventory)
	{
		std::vector<char> buffer;
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		writer << inventory;

		io::MemorySource source{ buffer };
		io::Reader reader{ source };

		uint16 itemCount = 0;
		reader >> io::read<uint16>(itemCount);
		return itemCount;
	}
}

TEST_CASE("Selling an item to a vendor puts it into a buyback slot", "[buyback]")
{
	BuyBackFixture fixture;
	Inventory& inventory = fixture.player->GetInventory();

	const uint64 itemGuid = fixture.PlaceItem();
	REQUIRE(inventory.GetItemAtSlot(fixture.FirstBackpackSlot()) != nullptr);

	REQUIRE(inventory.RemoveItem(fixture.FirstBackpackSlot(), 0, true) == inventory_change_failure::Okay);

	// The item left the backpack ...
	CHECK(inventory.GetItemAtSlot(fixture.FirstBackpackSlot()) == nullptr);

	uint16 soldSlot = 0;
	REQUIRE(inventory.FindItemByGUID(itemGuid, soldSlot));
	CHECK(InventorySlot::FromAbsolute(soldSlot).IsBuyBack());

	// ... but it is offered for buyback at the price the player was paid for it.
	const auto buyBackItem = inventory.GetBuyBackItem(0);
	REQUIRE(buyBackItem != nullptr);
	CHECK(buyBackItem->GetGuid() == itemGuid);
	CHECK(inventory.GetBuyBackPrice(0) == TestSellPrice);
}

TEST_CASE("A sold item can be bought back into the inventory", "[buyback]")
{
	BuyBackFixture fixture;
	Inventory& inventory = fixture.player->GetInventory();

	const uint64 itemGuid = fixture.PlaceItem();
	const uint16 countBeforeSale = inventory.GetItemCount(TestItemId);
	const uint16 freeSlotsBeforeSale = inventory.GetFreeSlotCount();

	REQUIRE(inventory.RemoveItem(fixture.FirstBackpackSlot(), 0, true) == inventory_change_failure::Okay);
	REQUIRE(inventory.GetBuyBackItem(0) != nullptr);

	REQUIRE(inventory.BuyBackItem(0) == inventory_change_failure::Okay);

	// The very same item instance is back in the inventory ...
	uint16 restoredSlot = 0;
	REQUIRE(inventory.FindItemByGUID(itemGuid, restoredSlot));
	CHECK(InventorySlot::FromAbsolute(restoredSlot).IsInventory());

	// ... and the sale plus the buyback left the item bookkeeping exactly as it was.
	CHECK(inventory.GetItemCount(TestItemId) == countBeforeSale);
	CHECK(inventory.GetFreeSlotCount() == freeSlotsBeforeSale);

	// ... and the buyback slot is free again, so the same item cannot be bought back twice.
	CHECK(inventory.GetBuyBackItem(0) == nullptr);
	CHECK(inventory.GetBuyBackPrice(0) == 0);
	CHECK(inventory.BuyBackItem(0) == inventory_change_failure::ItemNotFound);
}

TEST_CASE("Buying an item back is refused when there is no free inventory slot", "[buyback]")
{
	BuyBackFixture fixture;
	Inventory& inventory = fixture.player->GetInventory();

	fixture.PlaceItem();
	REQUIRE(inventory.RemoveItem(fixture.FirstBackpackSlot(), 0, true) == inventory_change_failure::Okay);

	// Fill every backpack slot, including the one the sold item came from.
	for (uint16 slot = player_inventory_pack_slots::Start; slot < player_inventory_pack_slots::End; ++slot)
	{
		auto filler = std::make_shared<GameItemS>(fixture.project, *fixture.entry);
		filler->Initialize();
		filler->Set<uint64>(object_fields::Guid, CreateEntryGUID(100 + slot, TestItemId, GuidType::Item));
		filler->Set<uint32>(object_fields::StackCount, 1);
		inventory.AddItemToSlot(filler, InventorySlot::FromRelative(player_inventory_slots::Bag_0, static_cast<uint8>(slot)).GetAbsolute());
	}

	CHECK(inventory.BuyBackItem(0) == inventory_change_failure::InventoryFull);

	// The offer survives a failed attempt.
	CHECK(inventory.GetBuyBackItem(0) != nullptr);
}

TEST_CASE("Buyback slots are never handed back to the realm", "[buyback]")
{
	BuyBackFixture fixture;
	Inventory& inventory = fixture.player->GetInventory();

	fixture.PlaceItem();
	REQUIRE(SerializedItemCount(inventory) == 1);

	REQUIRE(inventory.RemoveItem(fixture.FirstBackpackSlot(), 0, true) == inventory_change_failure::Okay);
	REQUIRE(inventory.GetBuyBackItem(0) != nullptr);

	// The buyback offer only lives inside this world session: logging out or changing the world
	// node must not carry it over, so it may not appear in the data sent back to the realm.
	CHECK(SerializedItemCount(inventory) == 0);
}
