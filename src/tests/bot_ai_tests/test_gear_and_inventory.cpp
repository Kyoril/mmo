// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "bot_ai/bot_gear.h"
#include "bot_core/bot_inventory.h"
#include "bot_core/bot_object_manager.h"

#include "game/item.h"
#include "proto_data/project.h"

using namespace mmo;

namespace
{
	proto::ItemEntry makeItem(const uint32 inventoryType, const uint32 itemLevel)
	{
		proto::ItemEntry item;
		item.set_id(1);
		item.set_name("test");
		item.set_inventorytype(inventoryType);
		item.set_itemlevel(itemLevel);
		return item;
	}

	void addStat(proto::ItemEntry& item, const uint32 type, const int32 value)
	{
		proto::ItemStats* stat = item.add_stats();
		stat->set_type(type);
		stat->set_value(value);
	}

	constexpr uint32 Mage = 0;
	constexpr uint32 Warrior = 1;
}

TEST_CASE("Items map to the equipment slots they fit", "[bot_ai][gear]")
{
	CHECK(EquipmentSlotsFor(inventory_type::Head) == std::vector<uint8> { player_equipment_slots::Head });
	CHECK(EquipmentSlotsFor(inventory_type::Robe) == std::vector<uint8> { player_equipment_slots::Chest });
	CHECK(EquipmentSlotsFor(inventory_type::Finger) == std::vector<uint8> { player_equipment_slots::Finger1, player_equipment_slots::Finger2 });
	CHECK(EquipmentSlotsFor(inventory_type::Shield) == std::vector<uint8> { player_equipment_slots::Offhand });
	CHECK(EquipmentSlotsFor(inventory_type::Bag).empty());
	CHECK(EquipmentSlotsFor(inventory_type::NonEquip).empty());
}

TEST_CASE("A class values its own primary stat most", "[bot_ai][gear]")
{
	proto::ItemEntry strength = makeItem(inventory_type::Chest, 10);
	addStat(strength, item_stat::Strength, 5);

	proto::ItemEntry intellect = makeItem(inventory_type::Chest, 10);
	addStat(intellect, item_stat::Intellect, 5);

	CHECK(ScoreItem(strength, Warrior) > ScoreItem(intellect, Warrior));
	CHECK(ScoreItem(intellect, Mage) > ScoreItem(strength, Mage));
}

TEST_CASE("Melee classes care about weapon damage, casters much less", "[bot_ai][gear]")
{
	proto::ItemEntry sword = makeItem(inventory_type::Weapon, 10);
	sword.set_delay(2000);
	sword.mutable_damage()->set_mindmg(10.0f);
	sword.mutable_damage()->set_maxdmg(20.0f);

	proto::ItemEntry stick = makeItem(inventory_type::Weapon, 10);
	stick.set_delay(2000);
	stick.mutable_damage()->set_mindmg(2.0f);
	stick.mutable_damage()->set_maxdmg(4.0f);

	const float warriorGain = ScoreItem(sword, Warrior) - ScoreItem(stick, Warrior);
	const float mageGain = ScoreItem(sword, Mage) - ScoreItem(stick, Mage);
	CHECK(warriorGain > 0.0f);
	CHECK(warriorGain > mageGain * 3.0f);
}

TEST_CASE("Junk is grey, wearables are what goes in an equipment slot", "[bot_ai][gear]")
{
	proto::ItemEntry grey = makeItem(inventory_type::NonEquip, 1);
	grey.set_quality(item_quality::Poor);
	CHECK(IsJunk(grey));
	CHECK_FALSE(IsWearable(grey));

	proto::ItemEntry boots = makeItem(inventory_type::Feet, 5);
	boots.set_quality(item_quality::Common);
	CHECK_FALSE(IsJunk(boots));
	CHECK(IsWearable(boots));
}

TEST_CASE("The inventory snapshot reads equipment, backpack and bags", "[bot_core][inventory]")
{
	BotObjectManager objects;

	const auto addItem = [&objects](const uint64 guid, const uint32 entry, const uint32 count)
	{
		BotItemState item;
		item.guid = guid;
		item.entry = entry;
		item.stackCount = count;
		objects.SetItem(item);
	};

	addItem(100, 7, 1);	// equipped
	addItem(101, 8, 3);	// backpack
	addItem(102, 9, 1);	// inside the bag

	BotItemState bag;
	bag.guid = 200;
	bag.entry = 50;
	bag.numSlots = 6;
	bag.slots.assign(36, 0);
	bag.slots[2] = 102;
	objects.SetItem(bag);

	objects.SetSelfInventorySlot(player_equipment_slots::Chest, 100);
	objects.SetSelfInventorySlot(player_inventory_pack_slots::Start + 4, 101);
	objects.SetSelfInventorySlot(player_inventory_slots::Start, 200);

	const BotInventory inventory = BotInventory::Snapshot(objects);

	// 16 backpack slots plus the 6 of the bag; one used in each.
	CHECK(inventory.GetTotalSlots() == 16 + 6);
	CHECK(inventory.GetFreeSlots() == 15 + 5);

	const BotInventoryItem* chest = inventory.GetEquipped(player_equipment_slots::Chest);
	REQUIRE(chest != nullptr);
	CHECK(chest->entry == 7);

	CHECK(inventory.CountInBags(8) == 3);
	CHECK(inventory.CountInBags(7) == 0);

	bool foundInBag = false;
	for (const BotInventoryItem& item : inventory.GetItems())
	{
		if (item.guid == 102)
		{
			foundInBag = true;
			CHECK(item.bag == player_inventory_slots::Start);
			CHECK(item.slot == 2);
			CHECK(item.GetAbsoluteSlot() == ((player_inventory_slots::Start << 8) | 2));
		}

		if (item.guid == 101)
		{
			CHECK(item.bag == player_inventory_slots::Bag_0);
			CHECK(item.slot == player_inventory_pack_slots::Start + 4);
		}
	}
	CHECK(foundInBag);
}

#include "bot_ai/grind_spot_claims.h"

TEST_CASE("Grind spot claims count bots per spot and follow them as they move", "[bot_ai][grind]")
{
	GrindSpotClaims claims;
	claims.Claim(1, 10);
	claims.Claim(2, 10);
	claims.Claim(3, 11);
	CHECK(claims.GetCount(10) == 2);
	CHECK(claims.GetCount(11) == 1);

	// A bot moving on frees its old spot.
	claims.Claim(1, 11);
	CHECK(claims.GetCount(10) == 1);
	CHECK(claims.GetCount(11) == 2);

	claims.Release(2);
	claims.Release(2);
	CHECK(claims.GetCount(10) == 0);
}
