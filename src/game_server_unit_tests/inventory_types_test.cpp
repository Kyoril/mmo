// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/inventory_types.h"

#include "catch.hpp"

using namespace mmo;

TEST_CASE("InventorySlot - Construction and basic accessors", "[inventory][types]")
{
	SECTION("FromAbsolute creates correct slot")
	{
		const auto slot = InventorySlot::FromAbsolute(0xFF10);
		REQUIRE(slot.GetAbsolute() == 0xFF10);
		REQUIRE(slot.GetBag() == 0xFF);
		REQUIRE(slot.GetSlot() == 0x10);
	}

	SECTION("FromRelative creates correct slot")
	{
		const auto slot = InventorySlot::FromRelative(255, 16);
		REQUIRE(slot.GetAbsolute() == 0xFF10);
		REQUIRE(slot.GetBag() == 255);
		REQUIRE(slot.GetSlot() == 16);
	}

	SECTION("FromRelative and FromAbsolute are equivalent")
	{
		const auto slot1 = InventorySlot::FromAbsolute(0x1305);
		const auto slot2 = InventorySlot::FromRelative(19, 5);
		REQUIRE(slot1.GetAbsolute() == slot2.GetAbsolute());
	}
}

TEST_CASE("InventorySlot - Slot type detection", "[inventory][types]")
{
	SECTION("IsEquipment detects equipment slots correctly")
	{
		// Equipment slots are bag 0xFF (255), slots 0-18
		const auto headSlot = InventorySlot::FromRelative(player_inventory_slots::Bag_0, player_equipment_slots::Head);
		REQUIRE(headSlot.IsEquipment());

		const auto mainhandSlot = InventorySlot::FromRelative(player_inventory_slots::Bag_0, player_equipment_slots::Mainhand);
		REQUIRE(mainhandSlot.IsEquipment());

		const auto tabardSlot = InventorySlot::FromRelative(player_inventory_slots::Bag_0, player_equipment_slots::Tabard);
		REQUIRE(tabardSlot.IsEquipment());

		// Non-equipment slot
		const auto bagSlot = InventorySlot::FromRelative(player_inventory_slots::Bag_0, player_inventory_slots::Start);
		REQUIRE_FALSE(bagSlot.IsEquipment());
	}

	SECTION("IsBagPack detects bag pack slots correctly")
	{
		// Bag pack slots are bag 0xFF (255), slots 19-22
		const auto bagSlot1 = InventorySlot::FromRelative(player_inventory_slots::Bag_0, player_inventory_slots::Start);
		REQUIRE(bagSlot1.IsBagPack());

		const auto bagSlot2 = InventorySlot::FromRelative(player_inventory_slots::Bag_0, player_inventory_slots::End - 1);
		REQUIRE(bagSlot2.IsBagPack());

		// Non-bag pack slot
		const auto equipSlot = InventorySlot::FromRelative(player_inventory_slots::Bag_0, player_equipment_slots::Head);
		REQUIRE_FALSE(equipSlot.IsBagPack());
	}

	SECTION("IsInventory detects backpack slots correctly")
	{
		// Inventory slots are bag 0xFF (255), slots 23-38
		const auto invSlot1 = InventorySlot::FromRelative(player_inventory_slots::Bag_0, player_inventory_pack_slots::Start);
		REQUIRE(invSlot1.IsInventory());

		const auto invSlot2 = InventorySlot::FromRelative(player_inventory_slots::Bag_0, player_inventory_pack_slots::End - 1);
		REQUIRE(invSlot2.IsInventory());

		// Non-inventory slot
		const auto equipSlot = InventorySlot::FromRelative(player_inventory_slots::Bag_0, player_equipment_slots::Head);
		REQUIRE_FALSE(equipSlot.IsInventory());
	}

	SECTION("IsBag detects equipped bag slots correctly")
	{
		// Bag slots are bags 19-22
		const auto bagSlot1 = InventorySlot::FromRelative(player_inventory_slots::Start, 0);
		REQUIRE(bagSlot1.IsBag());

		const auto bagSlot2 = InventorySlot::FromRelative(player_inventory_slots::End - 1, 0);
		REQUIRE(bagSlot2.IsBag());

		// Non-bag slot (main inventory)
		const auto mainInvSlot = InventorySlot::FromRelative(player_inventory_slots::Bag_0, player_inventory_pack_slots::Start);
		REQUIRE_FALSE(mainInvSlot.IsBag());
	}

	SECTION("IsBuyBack detects buyback slots correctly")
	{
		// Buyback slots are 74-85
		const auto buybackSlot1 = InventorySlot::FromAbsolute(player_buy_back_slots::Start);
		REQUIRE(buybackSlot1.IsBuyBack());

		const auto buybackSlot2 = InventorySlot::FromAbsolute(player_buy_back_slots::End - 1);
		REQUIRE(buybackSlot2.IsBuyBack());

		// Non-buyback slot
		const auto equipSlot = InventorySlot::FromRelative(player_inventory_slots::Bag_0, player_equipment_slots::Head);
		REQUIRE_FALSE(equipSlot.IsBuyBack());
	}
}

TEST_CASE("InventorySlot - Comparison operators", "[inventory][types]")
{
	const auto slot1 = InventorySlot::FromAbsolute(0xFF10);
	const auto slot2 = InventorySlot::FromAbsolute(0xFF10);
	const auto slot3 = InventorySlot::FromAbsolute(0xFF11);

	SECTION("Equality comparison")
	{
		REQUIRE(slot1 == slot2);
		REQUIRE_FALSE(slot1 == slot3);
	}

	SECTION("Inequality comparison")
	{
		REQUIRE(slot1 != slot3);
		REQUIRE_FALSE(slot1 != slot2);
	}

	SECTION("Less-than comparison")
	{
		REQUIRE(slot1 < slot3);
		REQUIRE_FALSE(slot3 < slot1);
		REQUIRE_FALSE(slot1 < slot2);
	}
}

TEST_CASE("ItemStack - Construction and basic operations", "[inventory][types]")
{
	SECTION("Construction with count")
	{
		const ItemStack stack(5);
		REQUIRE(stack.GetCount() == 5);
	}

	SECTION("CanAddStacks checks correctly")
	{
		const ItemStack fullStack(20);
		const ItemStack partialStack(15);

		REQUIRE_FALSE(fullStack.CanAddStacks(20));
		REQUIRE(partialStack.CanAddStacks(20));
	}

	SECTION("GetAvailableSpace calculates correctly")
	{
		const ItemStack stack(15);
		REQUIRE(stack.GetAvailableSpace(20) == 5);
		REQUIRE(stack.GetAvailableSpace(15) == 0);
		REQUIRE(stack.GetAvailableSpace(10) == 0);
	}

	SECTION("IsEmpty checks correctly")
	{
		const ItemStack emptyStack(0);
		const ItemStack nonEmptyStack(1);

		REQUIRE(emptyStack.IsEmpty());
		REQUIRE_FALSE(nonEmptyStack.IsEmpty());
	}

	SECTION("IsFull checks correctly")
	{
		const ItemStack fullStack(20);
		const ItemStack partialStack(15);

		REQUIRE(fullStack.IsFull(20));
		REQUIRE_FALSE(partialStack.IsFull(20));
	}
}

TEST_CASE("ItemStack - Mutation operations", "[inventory][types]")
{
	SECTION("Add increases count correctly")
	{
		ItemStack stack(15);
		const uint16 added = stack.Add(5, 20);

		REQUIRE(added == 5);
		REQUIRE(stack.GetCount() == 20);
	}

	SECTION("Add respects max stack size")
	{
		ItemStack stack(18);
		const uint16 added = stack.Add(5, 20);

		REQUIRE(added == 2);
		REQUIRE(stack.GetCount() == 20);
	}

	SECTION("Remove decreases count correctly")
	{
		ItemStack stack(15);
		const uint16 removed = stack.Remove(5);

		REQUIRE(removed == 5);
		REQUIRE(stack.GetCount() == 10);
	}

	SECTION("Remove doesn't go below zero")
	{
		ItemStack stack(5);
		const uint16 removed = stack.Remove(10);

		REQUIRE(removed == 5);
		REQUIRE(stack.GetCount() == 0);
	}
}

TEST_CASE("ItemStack - Comparison operators", "[inventory][types]")
{
	const ItemStack stack1(10);
	const ItemStack stack2(10);
	const ItemStack stack3(15);

	SECTION("Equality comparison")
	{
		REQUIRE(stack1 == stack2);
		REQUIRE_FALSE(stack1 == stack3);
	}

	SECTION("Inequality comparison")
	{
		REQUIRE(stack1 != stack3);
		REQUIRE_FALSE(stack1 != stack2);
	}
}

TEST_CASE("ItemCount - Basic operations", "[inventory][types]")
{
	SECTION("Construction and Get")
	{
		const ItemCount count(10);
		REQUIRE(count.Get() == 10);
		REQUIRE(count == 10);
	}

	SECTION("Add increases count")
	{
		ItemCount count(5);
		count.Add(3);
		REQUIRE(count.Get() == 8);
	}

	SECTION("Subtract decreases count")
	{
		ItemCount count(10);
		count.Subtract(3);
		REQUIRE(count.Get() == 7);
	}

	SECTION("Subtract doesn't go below zero")
	{
		ItemCount count(5);
		count.Subtract(10);
		REQUIRE(count.Get() == 0);
	}

	SECTION("IsZero checks correctly")
	{
		const ItemCount zeroCount(0);
		const ItemCount nonZeroCount(5);

		REQUIRE(zeroCount.IsZero());
		REQUIRE_FALSE(nonZeroCount.IsZero());
	}
}

TEST_CASE("SlotAvailability - Space checks", "[inventory][types]")
{
	SECTION("HasSpace detects empty slots")
	{
		SlotAvailability availability;
		availability.emptySlots = 1;

		REQUIRE(availability.HasSpace());
	}

	SECTION("HasSpace detects available stack space")
	{
		SlotAvailability availability;
		availability.availableStackSpace = 10;

		REQUIRE(availability.HasSpace());
	}

	SECTION("HasSpace returns false when no space")
	{
		const SlotAvailability availability;
		REQUIRE_FALSE(availability.HasSpace());
	}

	SECTION("CanAccommodate checks correctly")
	{
		SlotAvailability availability;
		availability.availableStackSpace = 20;

		REQUIRE(availability.CanAccommodate(15));
		REQUIRE(availability.CanAccommodate(20));
		REQUIRE_FALSE(availability.CanAccommodate(25));
	}
}

TEST_CASE("InventoryResult<void> - Success and failure", "[inventory][types]")
{
	SECTION("Success creates successful result")
	{
		const auto result = InventoryResult<void>::Success();

		REQUIRE(result.IsSuccess());
		REQUIRE_FALSE(result.IsFailure());
		REQUIRE(result.GetError() == inventory_change_failure::Okay);
	}

	SECTION("Failure creates failed result")
	{
		const auto result = InventoryResult<void>::Failure(inventory_change_failure::InventoryFull);

		REQUIRE_FALSE(result.IsSuccess());
		REQUIRE(result.IsFailure());
		REQUIRE(result.GetError() == inventory_change_failure::InventoryFull);
	}

	SECTION("OnSuccess executes only on success")
	{
		bool executed = false;
		auto result = InventoryResult<void>::Success();

		result.OnSuccess([&]()
		{
			executed = true;
		});

		REQUIRE(executed);
	}

	SECTION("OnSuccess doesn't execute on failure")
	{
		bool executed = false;
		auto result = InventoryResult<void>::Failure(inventory_change_failure::InventoryFull);

		result.OnSuccess([&]()
		{
			executed = true;
		});

		REQUIRE_FALSE(executed);
	}

	SECTION("OnFailure executes only on failure")
	{
		bool executed = false;
		auto result = InventoryResult<void>::Failure(inventory_change_failure::InventoryFull);

		result.OnFailure([&](InventoryChangeFailure error)
		{
			executed = true;
			REQUIRE(error == inventory_change_failure::InventoryFull);
		});

		REQUIRE(executed);
	}

	SECTION("OnFailure doesn't execute on success")
	{
		bool executed = false;
		auto result = InventoryResult<void>::Success();

		result.OnFailure([&](InventoryChangeFailure)
		{
			executed = true;
		});

		REQUIRE_FALSE(executed);
	}
}

TEST_CASE("InventoryResult<T> - Success and failure with value", "[inventory][types]")
{
	SECTION("Success creates result with value")
	{
		const auto result = InventoryResult<int>::Success(42);

		REQUIRE(result.IsSuccess());
		REQUIRE(result.GetValue().has_value());
		REQUIRE(*result.GetValue() == 42);
	}

	SECTION("Failure creates result without value")
	{
		const auto result = InventoryResult<int>::Failure(inventory_change_failure::ItemNotFound);

		REQUIRE(result.IsFailure());
		REQUIRE_FALSE(result.GetValue().has_value());
		REQUIRE(result.GetError() == inventory_change_failure::ItemNotFound);
	}

	SECTION("OnSuccess executes with value")
	{
		bool executed = false;
		int receivedValue = 0;
		auto result = InventoryResult<int>::Success(42);

		result.OnSuccess([&](int value)
		{
			executed = true;
			receivedValue = value;
		});

		REQUIRE(executed);
		REQUIRE(receivedValue == 42);
	}
}
