// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/bag_manager.h"
#include "game_server/inventory_types.h"
#include "game_server/objects/game_bag_s.h"
#include "game_server/objects/game_item_s.h"
#include "shared/proto_data/items.pb.h"
#include "shared/proto_data/project.h"

#include "catch.hpp"

#include <map>
#include <memory>

using namespace mmo;

namespace
{
	/**
	 * @brief Mock implementation of IBagManagerContext for testing.
	 */
	class MockBagManagerContext final : public IBagManagerContext
	{
	public:
		MockBagManagerContext()
			: m_ownerGuid(0x123456789ABCDEF0)
			, m_lastUpdatedSlot(0)
			, m_updateCount(0)
		{
		}

		// Test setup methods
		void SetItemAtSlot(uint16 slot, std::shared_ptr<GameItemS> item)
		{
			m_items[slot] = item;
		}

		// Test inspection methods
		uint16 GetLastUpdatedSlot() const { return m_lastUpdatedSlot; }
		uint32 GetUpdateCount() const { return m_updateCount; }

		// IBagManagerContext implementation
		std::shared_ptr<GameItemS> GetItemAtSlot(uint16 slot) const noexcept override
		{
			auto it = m_items.find(slot);
			return it != m_items.end() ? it->second : nullptr;
		}

		void NotifyItemUpdated(
			std::shared_ptr<GameItemS> item,
			uint16 slot) noexcept override
		{
			m_lastUpdatedSlot = slot;
			m_updateCount++;
		}

		uint64 GetOwnerGuid() const noexcept override
		{
			return m_ownerGuid;
		}

	private:
		uint64 m_ownerGuid;
		std::map<uint16, std::shared_ptr<GameItemS>> m_items;
		uint16 m_lastUpdatedSlot;
		uint32 m_updateCount;
	};

	/**
	 * @brief Helper to create a mock bag for testing.
	 */
	std::shared_ptr<GameBagS> CreateMockBag(uint32 entryId, uint32 slotCount)
	{
		proto::ItemEntry entry;
		entry.set_id(entryId);
		entry.set_itemclass(item_class::Container);
		entry.set_subclass(0);
		entry.set_inventorytype(inventory_type::Bag);
		entry.set_containerslots(slotCount);

		static proto::Project testProject;
		auto bag = std::make_shared<GameBagS>(testProject, entry);
		bag->Initialize();
		bag->Set<uint32>(object_fields::NumSlots, slotCount);
		bag->Set<uint64>(object_fields::Guid, 0x100 + entryId);

		return bag;
	}

	/**
	 * @brief Helper to create a mock item for testing.
	 */
	std::shared_ptr<GameItemS> CreateMockItem(uint32 entryId)
	{
		proto::ItemEntry entry;
		entry.set_id(entryId);
		entry.set_itemclass(item_class::Weapon);
		entry.set_subclass(1);
		entry.set_inventorytype(inventory_type::MainHandWeapon);

		static proto::Project testProject;
		auto item = std::make_shared<GameItemS>(testProject, entry);
		item->Initialize();
		item->Set<uint64>(object_fields::Guid, 0x200 + entryId);

		return item;
	}
}

TEST_CASE("BagManager - Get bag from slot", "[bag_manager]")
{
	MockBagManagerContext context;
	BagManager manager(context);

	SECTION("Returns bag when bag pack slot contains container")
	{
		auto bag = CreateMockBag(1000, 16);
		
		// Bag pack slot format: 0xFFBB where BB is bag slot (0-4)
		const uint16 bagPackSlot = (255 << 8) | 1;  // Bag_1
		const auto slot = InventorySlot::FromAbsolute(bagPackSlot);
		
		context.SetItemAtSlot(bagPackSlot, bag);

		const auto result = manager.GetBag(slot);

		REQUIRE(result != nullptr);
		REQUIRE(result->GetGuid() == bag->GetGuid());
		REQUIRE(result->GetSlotCount() == 16);
	}	SECTION("Returns nullptr when slot is empty")
	{
		const uint16 bagPackSlot = (255 << 8) | 2;  // Bag_2
		const auto slot = InventorySlot::FromAbsolute(bagPackSlot);

		const auto result = manager.GetBag(slot);

		REQUIRE(result == nullptr);
	}

	SECTION("Returns nullptr when slot contains non-container item")
	{
		auto item = CreateMockItem(2000);

		const uint16 bagPackSlot = (255 << 8) | 3;  // Bag_3
		const auto slot = InventorySlot::FromAbsolute(bagPackSlot);

		context.SetItemAtSlot(bagPackSlot, item);

		const auto result = manager.GetBag(slot);

		REQUIRE(result == nullptr);
	}

	SECTION("Converts bag slot to bag pack slot format")
	{
		auto bag = CreateMockBag(3000, 20);

		// Test with bag slot format (0xBBSS) - should convert to bag pack (0xFFBB)
		const uint16 bagSlot = (1 << 8) | 5;  // Bag 1, subslot 5
		const uint16 expectedBagPackSlot = (255 << 8) | 1;  // Converts to Bag_1

		context.SetItemAtSlot(expectedBagPackSlot, bag);

		const auto slot = InventorySlot::FromAbsolute(bagSlot);
		const auto result = manager.GetBag(slot);

		REQUIRE(result != nullptr);
		REQUIRE(result->GetGuid() == bag->GetGuid());
	}
}

TEST_CASE("BagManager - Update bag slot", "[bag_manager]")
{
	MockBagManagerContext context;
	BagManager manager(context);

	SECTION("Updates item reference in bag slot field")
	{
		auto bag = CreateMockBag(1000, 16);
		auto item = CreateMockItem(2000);

		const uint16 bagPackSlot = (255 << 8) | 2;  // Bag_2
		context.SetItemAtSlot(bagPackSlot, bag);

		manager.UpdateBagSlot(item, 2, 5);  // Bag 2, item slot 5

		// Verify bag's slot field was updated with item's GUID
		const uint64 slotFieldValue = bag->Get<uint64>(object_fields::Slot_1 + (5 * 2));
		REQUIRE(slotFieldValue == item->GetGuid());

		// Verify update notification was sent
		REQUIRE(context.GetUpdateCount() == 1);
		REQUIRE(context.GetLastUpdatedSlot() == bagPackSlot);
	}

	SECTION("Handles multiple item updates in same bag")
	{
		auto bag = CreateMockBag(1000, 16);
		auto item1 = CreateMockItem(2001);
		auto item2 = CreateMockItem(2002);
		auto item3 = CreateMockItem(2003);

		const uint16 bagPackSlot = (255 << 8) | 1;  // Bag_1
		context.SetItemAtSlot(bagPackSlot, bag);

		manager.UpdateBagSlot(item1, 1, 0);
		manager.UpdateBagSlot(item2, 1, 1);
		manager.UpdateBagSlot(item3, 1, 2);

		REQUIRE(bag->Get<uint64>(object_fields::Slot_1 + (0 * 2)) == item1->GetGuid());
		REQUIRE(bag->Get<uint64>(object_fields::Slot_1 + (1 * 2)) == item2->GetGuid());
		REQUIRE(bag->Get<uint64>(object_fields::Slot_1 + (2 * 2)) == item3->GetGuid());
		REQUIRE(context.GetUpdateCount() == 3);
	}

	SECTION("Does nothing when bag slot is empty")
	{
		auto item = CreateMockItem(2000);

		manager.UpdateBagSlot(item, 3, 0);  // Bag 3 doesn't exist

		// No update notification should have been sent
		REQUIRE(context.GetUpdateCount() == 0);
	}

	SECTION("Does nothing when slot contains non-container")
	{
		auto regularItem = CreateMockItem(1500);
		auto itemToAdd = CreateMockItem(2000);

		const uint16 bagPackSlot = (255 << 8) | 4;  // Bag_4
		context.SetItemAtSlot(bagPackSlot, regularItem);

		manager.UpdateBagSlot(itemToAdd, 4, 0);

		// No update notification should have been sent
		REQUIRE(context.GetUpdateCount() == 0);
	}
}

TEST_CASE("BagManager - Calculate equip bag slot change", "[bag_manager]")
{
	MockBagManagerContext context;
	BagManager manager(context);

	SECTION("Returns positive slot count for 16-slot bag")
	{
		auto bag = CreateMockBag(1000, 16);

		const int32 change = manager.CalculateEquipBagSlotChange(*bag);

		REQUIRE(change == 16);
	}

	SECTION("Returns positive slot count for 20-slot bag")
	{
		auto bag = CreateMockBag(2000, 20);

		const int32 change = manager.CalculateEquipBagSlotChange(*bag);

		REQUIRE(change == 20);
	}

	SECTION("Returns positive slot count for 6-slot bag")
	{
		auto bag = CreateMockBag(3000, 6);

		const int32 change = manager.CalculateEquipBagSlotChange(*bag);

		REQUIRE(change == 6);
	}
}

TEST_CASE("BagManager - Calculate unequip bag slot change", "[bag_manager]")
{
	MockBagManagerContext context;
	BagManager manager(context);

	SECTION("Returns negative slot count for 16-slot bag")
	{
		auto bag = CreateMockBag(1000, 16);

		const int32 change = manager.CalculateUnequipBagSlotChange(*bag);

		REQUIRE(change == -16);
	}

	SECTION("Returns negative slot count for 20-slot bag")
	{
		auto bag = CreateMockBag(2000, 20);

		const int32 change = manager.CalculateUnequipBagSlotChange(*bag);

		REQUIRE(change == -20);
	}

	SECTION("Returns negative slot count for 10-slot bag")
	{
		auto bag = CreateMockBag(3000, 10);

		const int32 change = manager.CalculateUnequipBagSlotChange(*bag);

		REQUIRE(change == -10);
	}
}

TEST_CASE("BagManager - Calculate swap bag slot change", "[bag_manager]")
{
	MockBagManagerContext context;
	BagManager manager(context);

	SECTION("Returns positive when new bag is larger")
	{
		auto oldBag = CreateMockBag(1000, 12);
		auto newBag = CreateMockBag(2000, 16);

		const int32 change = manager.CalculateSwapBagSlotChange(oldBag.get(), newBag.get());

		REQUIRE(change == 4);  // +16 - 12 = +4
	}

	SECTION("Returns negative when new bag is smaller")
	{
		auto oldBag = CreateMockBag(1000, 20);
		auto newBag = CreateMockBag(2000, 14);

		const int32 change = manager.CalculateSwapBagSlotChange(oldBag.get(), newBag.get());

		REQUIRE(change == -6);  // +14 - 20 = -6
	}

	SECTION("Returns zero when bags have same size")
	{
		auto oldBag = CreateMockBag(1000, 16);
		auto newBag = CreateMockBag(2000, 16);

		const int32 change = manager.CalculateSwapBagSlotChange(oldBag.get(), newBag.get());

		REQUIRE(change == 0);  // +16 - 16 = 0
	}

	SECTION("Returns positive when old bag is null (equipping)")
	{
		auto newBag = CreateMockBag(2000, 18);

		const int32 change = manager.CalculateSwapBagSlotChange(nullptr, newBag.get());

		REQUIRE(change == 18);  // +18 - 0 = +18
	}

	SECTION("Returns negative when new bag is null (unequipping)")
	{
		auto oldBag = CreateMockBag(1000, 16);

		const int32 change = manager.CalculateSwapBagSlotChange(oldBag.get(), nullptr);

		REQUIRE(change == -16);  // +0 - 16 = -16
	}

	SECTION("Returns zero when both bags are null")
	{
		const int32 change = manager.CalculateSwapBagSlotChange(nullptr, nullptr);

		REQUIRE(change == 0);
	}
}

TEST_CASE("BagManager - Bag pack slot detection", "[bag_manager]")
{
	MockBagManagerContext context;
	BagManager manager(context);

	SECTION("Recognizes bag pack slot format (0xFFxx)")
	{
		const uint16 bagPackSlot1 = (255 << 8) | 0;
		const uint16 bagPackSlot2 = (255 << 8) | 1;
		const uint16 bagPackSlot3 = (255 << 8) | 4;

		const auto slot1 = InventorySlot::FromAbsolute(bagPackSlot1);
		const auto slot2 = InventorySlot::FromAbsolute(bagPackSlot2);
		const auto slot3 = InventorySlot::FromAbsolute(bagPackSlot3);

		// These should be recognized as bag pack slots and not trigger conversion
		// We test this indirectly by checking that GetBag works correctly
		auto bag1 = CreateMockBag(1000, 16);
		auto bag2 = CreateMockBag(2000, 18);
		auto bag3 = CreateMockBag(3000, 20);

		context.SetItemAtSlot(bagPackSlot1, bag1);
		context.SetItemAtSlot(bagPackSlot2, bag2);
		context.SetItemAtSlot(bagPackSlot3, bag3);

		REQUIRE(manager.GetBag(slot1) != nullptr);
		REQUIRE(manager.GetBag(slot2) != nullptr);
		REQUIRE(manager.GetBag(slot3) != nullptr);
	}
}