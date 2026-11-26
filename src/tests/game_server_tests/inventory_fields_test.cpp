// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Which player field holds an inventory slot's item guid and its visible item: the old
// formulas for the regular slots, the appended fields for the glider slot.

#include "catch.hpp"

#include "game/inventory_fields.h"

using namespace mmo;

TEST_CASE("Inventory guid fields keep the old layout and map the glider slot to its own field", "[inventory_fields]")
{
	CHECK(GetInventorySlotField(player_equipment_slots::Head) == object_fields::InvSlotHead);
	CHECK(GetInventorySlotField(player_equipment_slots::Tabard) == object_fields::InvSlotHead + 18 * 2);
	CHECK(GetInventorySlotField(player_inventory_pack_slots::Start) == object_fields::PackSlot_1);
	CHECK(GetInventorySlotField(player_bank_item_slots::Start) == object_fields::BankSlot_1);
	CHECK(GetInventorySlotField(player_buy_back_slots::Start) == object_fields::VendorBuybackSlot_1);
	CHECK(GetInventorySlotField(player_glider_slots::Slot) == object_fields::GliderSlot);
	CHECK(GetInventorySlotField(player_glider_slots::Slot + 1) == InvalidField);
}

TEST_CASE("Visible item fields exist for the worn slots and the glider slot only", "[inventory_fields]")
{
	CHECK(GetVisibleItemField(player_equipment_slots::Head) == object_fields::VisibleItem1_0);
	CHECK(GetVisibleItemCreatorField(player_equipment_slots::Head) == object_fields::VisibleItem1_CREATOR);
	CHECK(GetVisibleItemField(player_equipment_slots::Tabard) == object_fields::VisibleItem19_0);
	CHECK(GetVisibleItemField(player_glider_slots::Slot) == object_fields::VisibleGlider_0);
	CHECK(GetVisibleItemCreatorField(player_glider_slots::Slot) == object_fields::VisibleGlider_CREATOR);
	CHECK(GetVisibleItemField(player_inventory_pack_slots::Start) == InvalidField);
	CHECK(GetVisibleItemField(player_bank_item_slots::Start) == InvalidField);
}

TEST_CASE("The glider fields are appended at the end of the player fields", "[inventory_fields]")
{
	CHECK(object_fields::GliderSlot == object_fields::MaxVigor + 1);
	CHECK(object_fields::PlayerFieldCount == object_fields::VisibleGlider_PROPERTIES + 1);
}

TEST_CASE("A change of the glider slot field counts as an equipment change", "[inventory_fields]")
{
	CHECK(FieldRangeChangesEquipment(object_fields::InvSlotHead, 2));
	CHECK(FieldRangeChangesEquipment(object_fields::GliderSlot, 2));
	CHECK(FieldRangeChangesEquipment(object_fields::GliderSlot + 1, 1));
	CHECK_FALSE(FieldRangeChangesEquipment(object_fields::PackSlot_1, 2));
	CHECK_FALSE(FieldRangeChangesEquipment(object_fields::VisibleGlider_0, 1));
}
