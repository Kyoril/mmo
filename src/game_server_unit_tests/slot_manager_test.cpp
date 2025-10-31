// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/slot_manager.h"
#include "game_server/inventory_types.h"
#include "game_server/objects/game_item_s.h"
#include "game_server/objects/game_bag_s.h"
#include "shared/proto_data/items.pb.h"

#include "catch.hpp"

#include <map>
#include <memory>

using namespace mmo;

namespace
{
	/**
	 * @brief Mock implementation of ISlotManagerContext for testing.
	 *
	 * Provides controlled inventory state for comprehensive unit testing
	 * of slot management operations. Allows fine-grained control over
	 * item placement, bag configuration, and slot occupancy.
	 */
	class MockSlotManagerContext final : public ISlotManagerContext
	{
	public:
		MockSlotManagerContext() = default;

		// Test setup methods
		void SetItemAtSlot(uint16 slot, std::shared_ptr<GameItemS> item)
		{
			m_items[slot] = item;
		}

		void SetBagAtSlot(uint16 slot, std::shared_ptr<GameBagS> bag)
		{
			m_bags[slot] = bag;
		}

		void SetItemCount(uint32 itemId, uint16 count)
		{
			m_itemCounts[itemId] = count;
		}

		void ClearAll()
		{
			m_items.clear();
			m_bags.clear();
			m_itemCounts.clear();
		}

		// ISlotManagerContext implementation
		std::shared_ptr<GameItemS> GetItemAtSlot(uint16 slot) const noexcept override
		{
			auto it = m_items.find(slot);
			return it != m_items.end() ? it->second : nullptr;
		}

		std::shared_ptr<GameBagS> GetBagAtSlot(uint16 slot) const noexcept override
		{
			auto it = m_bags.find(slot);
			return it != m_bags.end() ? it->second : nullptr;
		}

		uint16 GetItemCount(uint32 itemId) const noexcept override
		{
			auto it = m_itemCounts.find(itemId);
			return it != m_itemCounts.end() ? it->second : 0;
		}

	private:
		std::map<uint16, std::shared_ptr<GameItemS>> m_items;
		std::map<uint16, std::shared_ptr<GameBagS>> m_bags;
		std::map<uint32, uint16> m_itemCounts;
	};

	/**
	 * @brief Helper to create test item entries with common defaults.
	 */
	class ItemEntryBuilder
	{
	public:
		ItemEntryBuilder()
		{
			m_entry.set_id(1);
			m_entry.set_itemclass(item_class::Consumable);
			m_entry.set_subclass(item_subclass_consumable::Potion);
			m_entry.set_inventorytype(inventory_type::NonEquip);
			m_entry.set_maxcount(0);
			m_entry.set_maxstack(20);
		}

		ItemEntryBuilder& WithId(uint32 id)
		{
			m_entry.set_id(id);
			return *this;
		}

		ItemEntryBuilder& WithMaxStack(uint32 maxStack)
		{
			m_entry.set_maxstack(maxStack);
			return *this;
		}

		ItemEntryBuilder& WithMaxCount(uint32 maxCount)
		{
			m_entry.set_maxcount(maxCount);
			return *this;
		}

		proto::ItemEntry Build() const
		{
			return m_entry;
		}

	private:
		proto::ItemEntry m_entry;
	};

	/**
	 * @brief Mock GameItemS for testing purposes.
	 *
	 * Since GameItemS is not fully mockable, we use composition
	 * to control the entry used in tests.
	 */
	std::shared_ptr<GameItemS> CreateMockItem(const proto::ItemEntry& entry, uint16 stackCount = 1)
	{
		static proto::Project testProject;
		auto item = std::make_shared<GameItemS>(testProject, entry);
		item->Initialize();  // Must initialize field map before use
		
		// Add stacks if needed (default initialization sets stack to 1)
		if (stackCount > 1)
		{
			item->AddStacks(stackCount - 1);
		}
		
		return item;
	}

	/**
	 * @brief Mock GameBagS for testing purposes.
	 *
	 * Since GameBagS is final, we create real instances with test configurations.
	 */
	std::shared_ptr<GameBagS> CreateMockBag(uint8 slotCount)
	{
		static proto::Project testProject;
		
		proto::ItemEntry entry;
		entry.set_id(1000);
		entry.set_itemclass(item_class::Container);
		entry.set_containerslots(slotCount);
		
		auto bag = std::make_shared<GameBagS>(testProject, entry);
		bag->Initialize();  // Must initialize field map before use
		return bag;
	}
}

TEST_CASE("SlotManager - FindFirstEmptySlot", "[slot_manager]")
{
	MockSlotManagerContext context;
	SlotManager manager(context);

	SECTION("Finds first empty slot in main inventory")
	{
		const auto entry = ItemEntryBuilder().Build();
		
		// Occupy first 3 slots
		for (uint8 slot = 0; slot < 3; ++slot)
		{
			const auto absoluteSlot = InventorySlot::FromRelative(
				player_inventory_slots::Bag_0,
				player_inventory_pack_slots::Start + slot
			);
			context.SetItemAtSlot(absoluteSlot.GetAbsolute(), CreateMockItem(entry));
		}

		const auto result = manager.FindFirstEmptySlot();
		const auto expected = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_pack_slots::Start + 3
		);
		
		REQUIRE(result == expected.GetAbsolute());
	}

	SECTION("Returns 0 when all slots are full")
	{
		const auto entry = ItemEntryBuilder().Build();
		
		// Fill entire main inventory
		for (uint8 slot = player_inventory_pack_slots::Start; slot < player_inventory_pack_slots::End; ++slot)
		{
			const auto absoluteSlot = InventorySlot::FromRelative(player_inventory_slots::Bag_0, slot);
			context.SetItemAtSlot(absoluteSlot.GetAbsolute(), CreateMockItem(entry));
		}

		const auto result = manager.FindFirstEmptySlot();
		REQUIRE(result == 0);
	}

	SECTION("Finds empty slot in equipped bag")
	{
		const auto entry = ItemEntryBuilder().Build();
		
		// Fill main inventory
		for (uint8 slot = player_inventory_pack_slots::Start; slot < player_inventory_pack_slots::End; ++slot)
		{
			const auto absoluteSlot = InventorySlot::FromRelative(player_inventory_slots::Bag_0, slot);
			context.SetItemAtSlot(absoluteSlot.GetAbsolute(), CreateMockItem(entry));
		}

		// Add a bag with 16 slots to bag slot 0
		const auto bagSlot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_slots::Start
		);
		context.SetBagAtSlot(bagSlot.GetAbsolute(), CreateMockBag(16));

		const auto result = manager.FindFirstEmptySlot();
		const auto expected = InventorySlot::FromRelative(
			player_inventory_slots::Start,
			0
		);
		
		REQUIRE(result == expected.GetAbsolute());
	}

	SECTION("Returns first slot when inventory is empty")
	{
		const auto result = manager.FindFirstEmptySlot();
		const auto expected = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_pack_slots::Start
		);
		
		REQUIRE(result == expected.GetAbsolute());
	}
}

TEST_CASE("SlotManager - FindAvailableSlots for non-stackable items", "[slot_manager]")
{
	MockSlotManagerContext context;
	SlotManager manager(context);

	SECTION("Finds sufficient empty slots for non-stackable items")
	{
		const auto entry = ItemEntryBuilder()
			.WithMaxStack(1)
			.Build();

		SlotAllocationResult result;
		const auto status = manager.FindAvailableSlots(entry, 3, result);

		REQUIRE(status.IsSuccess());
		REQUIRE(result.emptySlots.size() >= 3);
		REQUIRE(result.usedCapableSlots.empty());
		REQUIRE(result.availableStacks >= 3);
	}

	SECTION("Fails when insufficient slots for non-stackable items")
	{
		const auto entry = ItemEntryBuilder()
			.WithMaxStack(1)
			.Build();

		// Fill all but 2 slots
		for (uint8 slot = player_inventory_pack_slots::Start; 
		     slot < player_inventory_pack_slots::End - 2; 
		     ++slot)
		{
			const auto absoluteSlot = InventorySlot::FromRelative(player_inventory_slots::Bag_0, slot);
			context.SetItemAtSlot(absoluteSlot.GetAbsolute(), CreateMockItem(entry));
		}

		SlotAllocationResult result;
		const auto status = manager.FindAvailableSlots(entry, 3, result);

		REQUIRE(status.IsFailure());
		REQUIRE(status.GetError() == inventory_change_failure::InventoryFull);
	}
}

TEST_CASE("SlotManager - FindAvailableSlots for stackable items", "[slot_manager]")
{
	MockSlotManagerContext context;
	SlotManager manager(context);

	SECTION("Uses existing stacks with available capacity")
	{
		const auto entry = ItemEntryBuilder()
			.WithId(100)
			.WithMaxStack(20)
			.Build();

		// Create two partial stacks (10 items each)
		for (uint8 i = 0; i < 2; ++i)
		{
			const auto slot = InventorySlot::FromRelative(
				player_inventory_slots::Bag_0,
				player_inventory_pack_slots::Start + i
			);
			auto item = CreateMockItem(entry);
			// Note: Stack count is determined by the entry's maxstack, not settable directly
			context.SetItemAtSlot(slot.GetAbsolute(), item);
		}
		context.SetItemCount(100, 20);

		SlotAllocationResult result;
		// Try to add 15 items (should fit in existing stacks)
		const auto status = manager.FindAvailableSlots(entry, 15, result);

		REQUIRE(status.IsSuccess());
		REQUIRE(result.usedCapableSlots.size() == 2);
		REQUIRE(result.availableStacks >= 15);
	}

	SECTION("Combines existing stacks and new slots")
	{
		const auto entry = ItemEntryBuilder()
			.WithId(100)
			.WithMaxStack(20)
			.Build();

		// Create one partial stack with 15 items
		const auto existingSlot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_pack_slots::Start
		);
		context.SetItemAtSlot(existingSlot.GetAbsolute(), CreateMockItem(entry));
		context.SetItemCount(100, 15);

		SlotAllocationResult result;
		// Try to add 30 items (5 fit in existing, need 25 more = 2 new slots)
		const auto status = manager.FindAvailableSlots(entry, 30, result);

		REQUIRE(status.IsSuccess());
		REQUIRE(result.usedCapableSlots.size() == 1);
		REQUIRE(result.emptySlots.size() >= 2);
		REQUIRE(result.availableStacks >= 30);
	}

	SECTION("Ignores full stacks")
	{
		const auto entry = ItemEntryBuilder()
			.WithId(100)
			.WithMaxStack(20)
			.Build();

		// Create two full stacks (20 items each)
		for (uint8 i = 0; i < 2; ++i)
		{
			const auto slot = InventorySlot::FromRelative(
				player_inventory_slots::Bag_0,
				player_inventory_pack_slots::Start + i
			);
			context.SetItemAtSlot(slot.GetAbsolute(), CreateMockItem(entry, 20));
		}
		context.SetItemCount(100, 40);

		SlotAllocationResult result;
		const auto status = manager.FindAvailableSlots(entry, 20, result);

		REQUIRE(status.IsSuccess());
		REQUIRE(result.usedCapableSlots.empty());  // Full stacks not included
		REQUIRE(result.emptySlots.size() >= 1);
	}

	SECTION("Correctly calculates required slots for large stacks")
	{
		const auto entry = ItemEntryBuilder()
			.WithMaxStack(20)
			.Build();

		SlotAllocationResult result;
		// 100 items / 20 per stack = 5 slots needed
		const auto status = manager.FindAvailableSlots(entry, 100, result);

		REQUIRE(status.IsSuccess());
		REQUIRE(result.emptySlots.size() >= 5);
		REQUIRE(result.availableStacks >= 100);
	}
}

TEST_CASE("SlotManager - ForEachBag", "[slot_manager]")
{
	MockSlotManagerContext context;
	SlotManager manager(context);

	SECTION("Iterates through main inventory when no bags equipped")
	{
		uint8 callCount = 0;
		
		manager.ForEachBag([&](uint8 bagId, uint8 startSlot, uint8 endSlot) -> bool
		{
			++callCount;
			REQUIRE(bagId == player_inventory_slots::Bag_0);
			REQUIRE(startSlot == player_inventory_pack_slots::Start);
			REQUIRE(endSlot == player_inventory_pack_slots::End);
			return true;
		});

		REQUIRE(callCount == 1);
	}

	SECTION("Iterates through main inventory and equipped bags")
	{
		// Equip bags in slots 0 and 2
		const auto bag0Slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_slots::Start
		);
		context.SetBagAtSlot(bag0Slot.GetAbsolute(), CreateMockBag(16));

		const auto bag2Slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_slots::Start + 2
		);
		context.SetBagAtSlot(bag2Slot.GetAbsolute(), CreateMockBag(12));

		std::vector<uint8> visitedBags;
		
		manager.ForEachBag([&](uint8 bagId, uint8 startSlot, uint8 endSlot) -> bool
		{
			visitedBags.push_back(bagId);
			return true;
		});

		REQUIRE(visitedBags.size() == 3);  // Main bag + 2 equipped bags
		REQUIRE(visitedBags[0] == player_inventory_slots::Bag_0);
		REQUIRE(visitedBags[1] == player_inventory_slots::Start);
		REQUIRE(visitedBags[2] == player_inventory_slots::Start + 2);
	}

	SECTION("Stops iteration when callback returns false")
	{
		// Equip all 4 bag slots
		for (uint8 i = 0; i < 4; ++i)
		{
			const auto bagSlot = InventorySlot::FromRelative(
				player_inventory_slots::Bag_0,
				player_inventory_slots::Start + i
			);
			context.SetBagAtSlot(bagSlot.GetAbsolute(), CreateMockBag(16));
		}

		uint8 callCount = 0;
		
		manager.ForEachBag([&](uint8 bagId, uint8 startSlot, uint8 endSlot) -> bool
		{
			++callCount;
			return callCount < 2;  // Stop after second call
		});

		REQUIRE(callCount == 2);
	}
}

TEST_CASE("SlotManager - CountFreeSlots", "[slot_manager]")
{
	MockSlotManagerContext context;
	SlotManager manager(context);

	SECTION("Counts all slots when inventory is empty")
	{
		const auto count = manager.CountFreeSlots();
		
		// Main inventory has 16 slots (slots 23-38)
		const auto expected = player_inventory_pack_slots::End - player_inventory_pack_slots::Start;
		REQUIRE(count == expected);
	}

	SECTION("Counts free slots excluding occupied ones")
	{
		const auto entry = ItemEntryBuilder().Build();
		
		// Occupy 5 slots
		for (uint8 i = 0; i < 5; ++i)
		{
			const auto slot = InventorySlot::FromRelative(
				player_inventory_slots::Bag_0,
				player_inventory_pack_slots::Start + i
			);
			context.SetItemAtSlot(slot.GetAbsolute(), CreateMockItem(entry));
		}

		const auto count = manager.CountFreeSlots();
		const auto expected = (player_inventory_pack_slots::End - player_inventory_pack_slots::Start) - 5;
		REQUIRE(count == expected);
	}

	SECTION("Includes equipped bag slots in count")
	{
		// Equip a 16-slot bag
		const auto bagSlot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_slots::Start
		);
		context.SetBagAtSlot(bagSlot.GetAbsolute(), CreateMockBag(16));

		const auto count = manager.CountFreeSlots();
		
		// Main inventory (16) + equipped bag (16) = 32
		const auto mainInvSlots = player_inventory_pack_slots::End - player_inventory_pack_slots::Start;
		const auto expected = mainInvSlots + 16;
		REQUIRE(count == expected);
	}
}

TEST_CASE("SlotManager - IsSlotEmpty", "[slot_manager]")
{
	MockSlotManagerContext context;
	SlotManager manager(context);

	SECTION("Returns true for empty slots")
	{
		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_pack_slots::Start
		);

		REQUIRE(manager.IsSlotEmpty(slot.GetAbsolute()));
	}

	SECTION("Returns false for occupied slots")
	{
		const auto entry = ItemEntryBuilder().Build();
		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_pack_slots::Start
		);
		
		context.SetItemAtSlot(slot.GetAbsolute(), CreateMockItem(entry));

		REQUIRE_FALSE(manager.IsSlotEmpty(slot.GetAbsolute()));
	}
}

TEST_CASE("SlotManager - GetBagSlotRange", "[slot_manager]")
{
	MockSlotManagerContext context;
	SlotManager manager(context);

	SECTION("Returns main inventory range for Bag_0")
	{
		uint8 startSlot, endSlot;
		const auto result = manager.GetBagSlotRange(player_inventory_slots::Bag_0, startSlot, endSlot);

		REQUIRE(result);
		REQUIRE(startSlot == player_inventory_pack_slots::Start);
		REQUIRE(endSlot == player_inventory_pack_slots::End);
	}

	SECTION("Returns false for unequipped bag slots")
	{
		uint8 startSlot, endSlot;
		const auto result = manager.GetBagSlotRange(player_inventory_slots::Start, startSlot, endSlot);

		REQUIRE_FALSE(result);
	}

	SECTION("Returns equipped bag range")
	{
		const auto bagSlot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_slots::Start
		);
		context.SetBagAtSlot(bagSlot.GetAbsolute(), CreateMockBag(20));

		uint8 startSlot, endSlot;
		const auto result = manager.GetBagSlotRange(player_inventory_slots::Start, startSlot, endSlot);

		REQUIRE(result);
		REQUIRE(startSlot == 0);
		REQUIRE(endSlot == 20);
	}
}
