// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game/item.h"

using namespace mmo;

TEST_CASE("Inventory type names resolve to their inventory type", "[item_comparison]")
{
	CHECK(GetInventoryTypeByName("HEAD") == inventory_type::Head);
	CHECK(GetInventoryTypeByName("FINGER") == inventory_type::Finger);
	CHECK(GetInventoryTypeByName("TWO_HANDED_WEAPON") == inventory_type::TwoHandedWeapon);
	CHECK(GetInventoryTypeByName("RELIC") == inventory_type::Relic);

	// Every name the item tooltip API hands out must round-trip.
	for (int32 type = 0; type < inventory_type::Count_; ++type)
	{
		ItemInfo info{};
		info.inventoryType = static_cast<uint32>(type);
		CHECK(GetInventoryTypeByName(info.GetItemInventoryTypeName()) == type);
	}

	// Unknown or missing names are treated as not equippable.
	CHECK(GetInventoryTypeByName("NOT_A_TYPE") == inventory_type::NonEquip);
	CHECK(GetInventoryTypeByName(nullptr) == inventory_type::NonEquip);
}

TEST_CASE("Single-slot equipment compares against the slot it is worn in", "[item_comparison]")
{
	CHECK(GetComparisonEquipmentSlot(inventory_type::Head, 0) == player_equipment_slots::Head);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Neck, 0) == player_equipment_slots::Neck);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Shoulders, 0) == player_equipment_slots::Shoulders);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Body, 0) == player_equipment_slots::Body);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Chest, 0) == player_equipment_slots::Chest);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Robe, 0) == player_equipment_slots::Chest);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Waist, 0) == player_equipment_slots::Waist);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Legs, 0) == player_equipment_slots::Legs);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Feet, 0) == player_equipment_slots::Feet);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Wrists, 0) == player_equipment_slots::Wrists);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Hands, 0) == player_equipment_slots::Hands);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Cloak, 0) == player_equipment_slots::Back);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Tabard, 0) == player_equipment_slots::Tabard);
	CHECK(GetComparisonEquipmentSlot(inventory_type::TwoHandedWeapon, 0) == player_equipment_slots::Mainhand);
	CHECK(GetComparisonEquipmentSlot(inventory_type::MainHandWeapon, 0) == player_equipment_slots::Mainhand);
	CHECK(GetComparisonEquipmentSlot(inventory_type::OffHandWeapon, 0) == player_equipment_slots::Offhand);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Shield, 0) == player_equipment_slots::Offhand);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Holdable, 0) == player_equipment_slots::Offhand);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Ranged, 0) == player_equipment_slots::Ranged);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Thrown, 0) == player_equipment_slots::Ranged);
	CHECK(GetComparisonEquipmentSlot(inventory_type::RangedRight, 0) == player_equipment_slots::Ranged);

	CHECK(GetComparisonEquipmentSlot(inventory_type::Head, 1) == -1);
	CHECK(GetComparisonEquipmentSlot(inventory_type::TwoHandedWeapon, 1) == -1);
}

TEST_CASE("Paired equipment compares against both slots", "[item_comparison]")
{
	CHECK(GetComparisonEquipmentSlot(inventory_type::Finger, 0) == player_equipment_slots::Finger1);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Finger, 1) == player_equipment_slots::Finger2);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Finger, 2) == -1);

	CHECK(GetComparisonEquipmentSlot(inventory_type::Trinket, 0) == player_equipment_slots::Trinket1);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Trinket, 1) == player_equipment_slots::Trinket2);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Trinket, 2) == -1);

	// One-handed weapons go into either hand.
	CHECK(GetComparisonEquipmentSlot(inventory_type::Weapon, 0) == player_equipment_slots::Mainhand);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Weapon, 1) == player_equipment_slots::Offhand);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Weapon, 2) == -1);
}

TEST_CASE("Items without an equipment slot have nothing to compare against", "[item_comparison]")
{
	CHECK(GetComparisonEquipmentSlot(inventory_type::NonEquip, 0) == -1);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Bag, 0) == -1);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Ammo, 0) == -1);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Quiver, 0) == -1);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Relic, 0) == -1);
	CHECK(GetComparisonEquipmentSlot(inventory_type::Count_, 0) == -1);
}
