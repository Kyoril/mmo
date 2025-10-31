// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/item_factory.h"
#include "game_server/inventory_types.h"
#include "game_server/objects/game_item_s.h"
#include "game_server/objects/game_bag_s.h"
#include "shared/proto_data/items.pb.h"
#include "shared/proto_data/project.h"

#include "catch.hpp"

#include <map>
#include <memory>

using namespace mmo;

namespace
{
	/**
	 * @brief Mock implementation of IItemFactoryContext for testing.
	 */
	class MockItemFactoryContext final : public IItemFactoryContext
	{
	public:
		MockItemFactoryContext()
			: m_nextItemId(1000)
			, m_ownerGuid(0x0000000100000001ULL)
		{
		}

		// Test setup methods
		void SetNextItemId(uint64 id) { m_nextItemId = id; }
		void SetOwnerGuid(uint64 guid) { m_ownerGuid = guid; }

		void AddBagAtSlot(uint16 slot, std::shared_ptr<GameBagS> bag)
		{
			m_bags[slot] = bag;
		}

		// IItemFactoryContext implementation
		uint64 GenerateItemId() const noexcept override
		{
			return m_nextItemId++;
		}

		uint64 GetOwnerGuid() const noexcept override
		{
			return m_ownerGuid;
		}

		const proto::Project& GetProject() const noexcept override
		{
			return m_project;
		}

		std::shared_ptr<GameBagS> GetBagAtSlot(uint16 slot) const noexcept override
		{
			auto it = m_bags.find(slot);
			return it != m_bags.end() ? it->second : nullptr;
		}

	private:
		mutable uint64 m_nextItemId;
		uint64 m_ownerGuid;
		proto::Project m_project;
		std::map<uint16, std::shared_ptr<GameBagS>> m_bags;
	};

	/**
	 * @brief Helper to build test item entries.
	 */
	class ItemEntryBuilder
	{
	public:
		ItemEntryBuilder()
		{
			m_entry.set_id(100);
			m_entry.set_itemclass(item_class::Consumable);
			m_entry.set_subclass(item_subclass_consumable::Potion);
			m_entry.set_inventorytype(inventory_type::NonEquip);
			m_entry.set_maxcount(0);
			m_entry.set_maxstack(20);
			m_entry.set_bonding(item_binding::NoBinding);
		}

		ItemEntryBuilder& WithId(uint32 id)
		{
			m_entry.set_id(id);
			return *this;
		}

		ItemEntryBuilder& WithClass(item_class::Type itemClass)
		{
			m_entry.set_itemclass(itemClass);
			return *this;
		}

		ItemEntryBuilder& WithMaxStack(uint32 maxStack)
		{
			m_entry.set_maxstack(maxStack);
			return *this;
		}

		ItemEntryBuilder& WithBinding(item_binding::Type binding)
		{
			m_entry.set_bonding(binding);
			return *this;
		}

		ItemEntryBuilder& WithContainerSlots(uint32 slots)
		{
			m_entry.set_containerslots(slots);
			return *this;
		}

		proto::ItemEntry Build() const
		{
			return m_entry;
		}

	private:
		proto::ItemEntry m_entry;
	};
}

TEST_CASE("ItemFactory - Basic item creation", "[item_factory]")
{
	MockItemFactoryContext context;
	ItemFactory factory(context);

	SECTION("Creates regular item with default stack count")
	{
		const auto entry = ItemEntryBuilder()
			.WithId(100)
			.WithClass(item_class::Consumable)
			.Build();

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_pack_slots::Start
		);

		auto item = factory.CreateItem(entry, slot);

		REQUIRE(item != nullptr);
		REQUIRE(item->GetEntry().id() == 100);
		REQUIRE(item->GetStackCount() == 1);
	}

	SECTION("Creates item with specified stack count")
	{
		const auto entry = ItemEntryBuilder()
			.WithMaxStack(20)
			.Build();

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_pack_slots::Start
		);

		auto item = factory.CreateItem(entry, slot, 15);

		REQUIRE(item != nullptr);
		REQUIRE(item->GetStackCount() == 15);
	}

	SECTION("Creates item with full stack")
	{
		const auto entry = ItemEntryBuilder()
			.WithMaxStack(20)
			.Build();

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_pack_slots::Start
		);

		auto item = factory.CreateItem(entry, slot, 20);

		REQUIRE(item != nullptr);
		REQUIRE(item->GetStackCount() == 20);
	}
}

TEST_CASE("ItemFactory - Container creation", "[item_factory]")
{
	MockItemFactoryContext context;
	ItemFactory factory(context);

	SECTION("Creates GameBagS for container item class")
	{
		const auto entry = ItemEntryBuilder()
			.WithClass(item_class::Container)
			.WithContainerSlots(16)
			.Build();

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_slots::Start
		);

		auto item = factory.CreateItem(entry, slot);

		REQUIRE(item != nullptr);

		// Should be a GameBagS instance
		auto bag = std::dynamic_pointer_cast<GameBagS>(item);
		REQUIRE(bag != nullptr);
		REQUIRE(bag->GetSlotCount() == 16);
	}

	SECTION("Creates GameBagS for quiver item class")
	{
		const auto entry = ItemEntryBuilder()
			.WithClass(item_class::Quiver)
			.WithContainerSlots(20)
			.Build();

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_slots::Start + 1
		);

		auto item = factory.CreateItem(entry, slot);

		REQUIRE(item != nullptr);

		auto bag = std::dynamic_pointer_cast<GameBagS>(item);
		REQUIRE(bag != nullptr);
		REQUIRE(bag->GetSlotCount() == 20);
	}
}

TEST_CASE("ItemFactory - GUID assignment", "[item_factory]")
{
	MockItemFactoryContext context;
	ItemFactory factory(context);

	SECTION("Assigns unique GUID to each item")
	{
		context.SetNextItemId(5000);

		const auto entry = ItemEntryBuilder().Build();
		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_pack_slots::Start
		);

		auto item1 = factory.CreateItem(entry, slot);
		auto item2 = factory.CreateItem(entry, slot);

		REQUIRE(item1->GetGuid() != 0);
		REQUIRE(item2->GetGuid() != 0);
		REQUIRE(item1->GetGuid() != item2->GetGuid());
	}

	SECTION("GUID contains correct entry ID")
	{
		const auto entry = ItemEntryBuilder()
			.WithId(12345)
			.Build();

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_pack_slots::Start
		);

		auto item = factory.CreateItem(entry, slot);

		// Extract entry from GUID (entry is shifted left by 24 bits)
		const uint64 guid = item->GetGuid();
		const uint32 entryFromGuid = static_cast<uint32>((guid >> 24) & 0x0FFFFFFF);

		REQUIRE(entryFromGuid == 12345);
	}
}

TEST_CASE("ItemFactory - Owner assignment", "[item_factory]")
{
	MockItemFactoryContext context;
	ItemFactory factory(context);

	SECTION("Assigns owner GUID to created items")
	{
		const uint64 playerGuid = 0x0000000200000042ULL;
		context.SetOwnerGuid(playerGuid);

		const auto entry = ItemEntryBuilder().Build();
		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_pack_slots::Start
		);

		auto item = factory.CreateItem(entry, slot);

		REQUIRE(item->Get<uint64>(object_fields::ItemOwner) == playerGuid);
	}
}

TEST_CASE("ItemFactory - Container assignment", "[item_factory]")
{
	MockItemFactoryContext context;
	ItemFactory factory(context);

	SECTION("Item in main inventory uses owner as container")
	{
		const uint64 ownerGuid = 0x0000000100000001ULL;
		context.SetOwnerGuid(ownerGuid);

		const auto entry = ItemEntryBuilder().Build();
		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_pack_slots::Start
		);

		auto item = factory.CreateItem(entry, slot);

		REQUIRE(item->Get<uint64>(object_fields::Contained) == ownerGuid);
	}

	SECTION("Item in equipped bag uses bag as container")
	{
		const uint64 ownerGuid = 0x0000000100000001ULL;
		context.SetOwnerGuid(ownerGuid);

		// Create and setup a bag
		const auto bagEntry = ItemEntryBuilder()
			.WithClass(item_class::Container)
			.WithContainerSlots(16)
			.Build();

		const auto bagSlot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_slots::Start
		);

		auto bag = factory.CreateItem(bagEntry, bagSlot);
		auto bagInstance = std::dynamic_pointer_cast<GameBagS>(bag);
		REQUIRE(bagInstance != nullptr);

		// Register bag in context
		context.AddBagAtSlot(bagSlot.GetAbsolute(), bagInstance);

		// Create item in the bag
		const auto itemEntry = ItemEntryBuilder().Build();
		const auto itemSlot = InventorySlot::FromRelative(
			player_inventory_slots::Start,
			0
		);

		auto item = factory.CreateItem(itemEntry, itemSlot);

		// Item should reference the bag as its container
		REQUIRE(item->Get<uint64>(object_fields::Contained) == bag->GetGuid());
	}

	SECTION("Item in bag without registered bag instance uses owner")
	{
		const uint64 ownerGuid = 0x0000000100000001ULL;
		context.SetOwnerGuid(ownerGuid);

		const auto entry = ItemEntryBuilder().Build();
		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Start,
			5
		);

		auto item = factory.CreateItem(entry, slot);

		// Should fall back to owner GUID
		REQUIRE(item->Get<uint64>(object_fields::Contained) == ownerGuid);
	}
}

TEST_CASE("ItemFactory - Binding rules", "[item_factory]")
{
	MockItemFactoryContext context;
	ItemFactory factory(context);

	SECTION("Applies Bind-on-Pickup binding")
	{
		const auto entry = ItemEntryBuilder()
			.WithBinding(item_binding::BindWhenPickedUp)
			.Build();

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_pack_slots::Start
		);

		auto item = factory.CreateItem(entry, slot);

		const uint32 flags = item->Get<uint32>(object_fields::ItemFlags);
		REQUIRE((flags & item_flags::Bound) != 0);
	}

	SECTION("Does not bind items without Bind-on-Pickup")
	{
		const auto entry = ItemEntryBuilder()
			.WithBinding(item_binding::NoBinding)
			.Build();

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_pack_slots::Start
		);

		auto item = factory.CreateItem(entry, slot);

		const uint32 flags = item->Get<uint32>(object_fields::ItemFlags);
		REQUIRE((flags & item_flags::Bound) == 0);
	}

	SECTION("Does not bind Bind-on-Equip items on creation")
	{
		const auto entry = ItemEntryBuilder()
			.WithBinding(item_binding::BindWhenEquipped)
			.Build();

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_pack_slots::Start
		);

		auto item = factory.CreateItem(entry, slot);

		const uint32 flags = item->Get<uint32>(object_fields::ItemFlags);
		REQUIRE((flags & item_flags::Bound) == 0);
	}
}

TEST_CASE("ItemFactory - Field initialization", "[item_factory]")
{
	MockItemFactoryContext context;
	ItemFactory factory(context);

	SECTION("Item has initialized field map")
	{
		const auto entry = ItemEntryBuilder().Build();
		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_pack_slots::Start
		);

		auto item = factory.CreateItem(entry, slot);

		// All these fields should be accessible without crashing
		REQUIRE(item->GetGuid() != 0);
		REQUIRE(item->Get<uint32>(object_fields::Entry) == entry.id());
		REQUIRE(item->Get<float>(object_fields::Scale) == 1.0f);
		REQUIRE(item->GetStackCount() == 1);
	}
}
