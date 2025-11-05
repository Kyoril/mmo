// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/equipment_manager.h"
#include "game_server/inventory_types.h"
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
	 * @brief Mock implementation of IEquipmentManagerContext for testing.
	 */
	class MockEquipmentManagerContext final : public IEquipmentManagerContext
	{
	public:
		MockEquipmentManagerContext()
			: m_level(60)
			, m_weaponProficiency(0xFFFFFFFF)  // All weapon proficiencies
			, m_armorProficiency(0xFFFFFFFF)   // All armor proficiencies
			, m_canDualWield(true)
			, m_statsApplied(false)
			, m_itemSetEquipped(false)
		{
		}

		// Test setup methods
		void SetLevel(uint32 level) { m_level = level; }
		void SetWeaponProficiency(uint32 prof) { m_weaponProficiency = prof; }
		void SetArmorProficiency(uint32 prof) { m_armorProficiency = prof; }
		void SetCanDualWield(bool canDualWield) { m_canDualWield = canDualWield; }

		void SetItemAtSlot(uint16 slot, std::shared_ptr<GameItemS> item)
		{
			m_items[slot] = item;
		}

		// Test inspection methods
		bool WasStatsApplied() const { return m_statsApplied; }
		bool WasItemSetEquipped() const { return m_itemSetEquipped; }
		uint8 GetLastVisualSlot() const { return m_lastVisualSlot; }
		uint32 GetLastVisualEntryId() const { return m_lastVisualEntryId; }

		// IEquipmentManagerContext implementation
		uint32 GetLevel() const noexcept override
		{
			return m_level;
		}

		uint32 GetWeaponProficiency() const noexcept override
		{
			return m_weaponProficiency;
		}

		uint32 GetArmorProficiency() const noexcept override
		{
			return m_armorProficiency;
		}

		bool CanDualWield() const noexcept override
		{
			return m_canDualWield;
		}

		std::shared_ptr<GameItemS> GetItemAtSlot(uint16 slot) const noexcept override
		{
			auto it = m_items.find(slot);
			return it != m_items.end() ? it->second : nullptr;
		}

		void ApplyItemStats(const GameItemS& item, bool apply) noexcept override
		{
			m_statsApplied = apply;
		}

		void UpdateEquipmentVisual(
			uint8 equipSlot,
			uint32 entryId,
			uint64 creatorGuid) noexcept override
		{
			m_lastVisualSlot = equipSlot;
			m_lastVisualEntryId = entryId;
			m_lastVisualCreatorGuid = creatorGuid;
		}

		void HandleItemSetEffect(uint32 itemSetId, bool equipped) noexcept override
		{
			m_itemSetEquipped = equipped;
			m_lastItemSetId = itemSetId;
		}

	private:
		uint32 m_level;
		uint32 m_weaponProficiency;
		uint32 m_armorProficiency;
		bool m_canDualWield;
		std::map<uint16, std::shared_ptr<GameItemS>> m_items;

		// Test tracking
		bool m_statsApplied;
		bool m_itemSetEquipped;
		uint8 m_lastVisualSlot;
		uint32 m_lastVisualEntryId;
		uint64 m_lastVisualCreatorGuid;
		uint32 m_lastItemSetId;
	};

	/**
	 * @brief Helper to build test item entries.
	 */
	class ItemEntryBuilder
	{
	public:
		ItemEntryBuilder()
		{
			m_entry.set_id(1000);
			m_entry.set_itemclass(item_class::Armor);
			m_entry.set_subclass(1);
			m_entry.set_inventorytype(inventory_type::Head);
			m_entry.set_requiredlevel(0);
			m_entry.set_bonding(item_binding::NoBinding);
			m_entry.set_itemset(0);
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

		ItemEntryBuilder& WithSubclass(uint32 subclass)
		{
			m_entry.set_subclass(subclass);
			return *this;
		}

		ItemEntryBuilder& WithInventoryType(inventory_type::Type invType)
		{
			m_entry.set_inventorytype(invType);
			return *this;
		}

		ItemEntryBuilder& WithRequiredLevel(uint32 level)
		{
			m_entry.set_requiredlevel(level);
			return *this;
		}

		ItemEntryBuilder& WithBinding(item_binding::Type binding)
		{
			m_entry.set_bonding(binding);
			return *this;
		}

		ItemEntryBuilder& WithItemSet(uint32 itemSetId)
		{
			m_entry.set_itemset(itemSetId);
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
	 * @brief Helper to create a mock GameItemS for testing.
	 */
	std::shared_ptr<GameItemS> CreateMockItem(const proto::ItemEntry& entry)
	{
		static proto::Project testProject;
		auto item = std::make_shared<GameItemS>(testProject, entry);
		item->Initialize();
		return item;
	}
}

TEST_CASE("EquipmentManager - Slot compatibility validation", "[equipment_manager]")
{
	MockEquipmentManagerContext context;
	EquipmentManager manager(context);

	SECTION("Validates head slot accepts head items")
	{
		const auto entry = ItemEntryBuilder()
			.WithInventoryType(inventory_type::Head)
			.Build();

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Head
		);

		const auto result = manager.ValidateEquipment(entry, slot);
		REQUIRE(result.IsSuccess());
	}

	SECTION("Rejects wrong item type for head slot")
	{
		const auto entry = ItemEntryBuilder()
			.WithInventoryType(inventory_type::Chest)
			.Build();

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Head
		);

		const auto result = manager.ValidateEquipment(entry, slot);
		REQUIRE(result.IsFailure());
		REQUIRE(result.GetError() == inventory_change_failure::ItemDoesNotGoToSlot);
	}

	SECTION("Validates chest slot accepts chest and robe items")
	{
		const auto chestEntry = ItemEntryBuilder()
			.WithInventoryType(inventory_type::Chest)
			.Build();

		const auto robeEntry = ItemEntryBuilder()
			.WithInventoryType(inventory_type::Robe)
			.Build();

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Chest
		);

		REQUIRE(manager.ValidateEquipment(chestEntry, slot).IsSuccess());
		REQUIRE(manager.ValidateEquipment(robeEntry, slot).IsSuccess());
	}

	SECTION("Validates finger slots accept finger items")
	{
		const auto entry = ItemEntryBuilder()
			.WithInventoryType(inventory_type::Finger)
			.Build();

		const auto finger1 = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Finger1
		);

		const auto finger2 = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Finger2
		);

		REQUIRE(manager.ValidateEquipment(entry, finger1).IsSuccess());
		REQUIRE(manager.ValidateEquipment(entry, finger2).IsSuccess());
	}

	SECTION("Validates trinket slots accept trinket items")
	{
		const auto entry = ItemEntryBuilder()
			.WithInventoryType(inventory_type::Trinket)
			.Build();

		const auto trinket1 = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Trinket1
		);

		const auto trinket2 = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Trinket2
		);

		REQUIRE(manager.ValidateEquipment(entry, trinket1).IsSuccess());
		REQUIRE(manager.ValidateEquipment(entry, trinket2).IsSuccess());
	}
}

TEST_CASE("EquipmentManager - Level requirement validation", "[equipment_manager]")
{
	MockEquipmentManagerContext context;
	EquipmentManager manager(context);

	SECTION("Allows equipping when level requirement is met")
	{
		context.SetLevel(60);

		const auto entry = ItemEntryBuilder()
			.WithInventoryType(inventory_type::Head)
			.WithRequiredLevel(50)
			.Build();

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Head
		);

		const auto result = manager.ValidateEquipment(entry, slot);
		REQUIRE(result.IsSuccess());
	}

	SECTION("Rejects equipping when level requirement is not met")
	{
		context.SetLevel(40);

		const auto entry = ItemEntryBuilder()
			.WithInventoryType(inventory_type::Head)
			.WithRequiredLevel(50)
			.Build();

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Head
		);

		const auto result = manager.ValidateEquipment(entry, slot);
		REQUIRE(result.IsFailure());
		REQUIRE(result.GetError() == inventory_change_failure::CantEquipLevel);
	}

	SECTION("Allows equipping items with no level requirement")
	{
		context.SetLevel(1);

		const auto entry = ItemEntryBuilder()
			.WithInventoryType(inventory_type::Head)
			.WithRequiredLevel(0)
			.Build();

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Head
		);

		const auto result = manager.ValidateEquipment(entry, slot);
		REQUIRE(result.IsSuccess());
	}
}

TEST_CASE("EquipmentManager - Proficiency validation", "[equipment_manager]")
{
	MockEquipmentManagerContext context;
	EquipmentManager manager(context);

	SECTION("Validates weapon proficiency")
	{
		context.SetWeaponProficiency(1 << item_subclass_weapon::OneHandedAxe);  // Only first weapon type

		const auto validEntry = ItemEntryBuilder()
			.WithClass(item_class::Weapon)
			.WithSubclass(item_subclass_weapon::OneHandedAxe)  // Has proficiency
			.WithInventoryType(inventory_type::MainHandWeapon)
			.Build();

		const auto invalidEntry = ItemEntryBuilder()
			.WithClass(item_class::Weapon)
			.WithSubclass(item_subclass_weapon::TwoHandedAxe)  // No proficiency
			.WithInventoryType(inventory_type::MainHandWeapon)
			.Build();

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Mainhand
		);

		REQUIRE(manager.ValidateEquipment(validEntry, slot).IsSuccess());

		const auto result = manager.ValidateEquipment(invalidEntry, slot);
		REQUIRE(result.IsFailure());
		REQUIRE(result.GetError() == inventory_change_failure::NoRequiredProficiency);
	}

	SECTION("Validates armor proficiency")
	{
		context.SetArmorProficiency((1 << item_subclass_armor::Cloth));  // Only first armor type

		const auto validEntry = ItemEntryBuilder()
			.WithClass(item_class::Armor)
			.WithSubclass(item_subclass_armor::Cloth)  // Has proficiency
			.WithInventoryType(inventory_type::Chest)
			.Build();

		const auto invalidEntry = ItemEntryBuilder()
			.WithClass(item_class::Armor)
			.WithSubclass(item_subclass_armor::Leather)  // No proficiency
			.WithInventoryType(inventory_type::Chest)
			.Build();

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Chest
		);

		REQUIRE(manager.ValidateEquipment(validEntry, slot).IsSuccess());

		const auto result = manager.ValidateEquipment(invalidEntry, slot);
		REQUIRE(result.IsFailure());
		REQUIRE(result.GetError() == inventory_change_failure::NoRequiredProficiency);
	}
}

TEST_CASE("EquipmentManager - Weapon slot validation", "[equipment_manager]")
{
	MockEquipmentManagerContext context;
	EquipmentManager manager(context);

	SECTION("Mainhand accepts main hand and two-handed weapons")
	{
		const auto mainHandEntry = ItemEntryBuilder()
			.WithClass(item_class::Weapon)
			.WithInventoryType(inventory_type::MainHandWeapon)
			.Build();

		const auto twoHandEntry = ItemEntryBuilder()
			.WithClass(item_class::Weapon)
			.WithInventoryType(inventory_type::TwoHandedWeapon)
			.Build();

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Mainhand
		);

		REQUIRE(manager.ValidateEquipment(mainHandEntry, slot).IsSuccess());
		REQUIRE(manager.ValidateEquipment(twoHandEntry, slot).IsSuccess());
	}

	SECTION("Offhand accepts offhand weapons and shields")
	{
		const auto offhandEntry = ItemEntryBuilder()
			.WithClass(item_class::Weapon)
			.WithInventoryType(inventory_type::OffHandWeapon)
			.Build();

		const auto shieldEntry = ItemEntryBuilder()
			.WithClass(item_class::Armor)
			.WithInventoryType(inventory_type::Shield)
			.Build();

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Offhand
		);

		REQUIRE(manager.ValidateEquipment(offhandEntry, slot).IsSuccess());
		REQUIRE(manager.ValidateEquipment(shieldEntry, slot).IsSuccess());
	}

	SECTION("Ranged slot accepts ranged and thrown weapons")
	{
		const auto rangedEntry = ItemEntryBuilder()
			.WithClass(item_class::Weapon)
			.WithInventoryType(inventory_type::Ranged)
			.Build();

		const auto thrownEntry = ItemEntryBuilder()
			.WithClass(item_class::Weapon)
			.WithInventoryType(inventory_type::Thrown)
			.Build();

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Ranged
		);

		REQUIRE(manager.ValidateEquipment(rangedEntry, slot).IsSuccess());
		REQUIRE(manager.ValidateEquipment(thrownEntry, slot).IsSuccess());
	}
}

TEST_CASE("EquipmentManager - Dual wield validation", "[equipment_manager]")
{
	MockEquipmentManagerContext context;
	EquipmentManager manager(context);

	SECTION("Allows shield in offhand without dual wield")
	{
		context.SetCanDualWield(false);

		const auto entry = ItemEntryBuilder()
			.WithClass(item_class::Armor)
			.WithInventoryType(inventory_type::Shield)
			.Build();

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Offhand
		);

		const auto result = manager.ValidateEquipment(entry, slot);
		REQUIRE(result.IsSuccess());
	}

	SECTION("Allows holdable in offhand without dual wield")
	{
		context.SetCanDualWield(false);

		const auto entry = ItemEntryBuilder()
			.WithInventoryType(inventory_type::Holdable)
			.Build();

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Offhand
		);

		const auto result = manager.ValidateEquipment(entry, slot);
		REQUIRE(result.IsSuccess());
	}

	SECTION("Rejects weapon in offhand without dual wield")
	{
		context.SetCanDualWield(false);

		const auto entry = ItemEntryBuilder()
			.WithClass(item_class::Weapon)
			.WithInventoryType(inventory_type::OffHandWeapon)
			.Build();

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Offhand
		);

		const auto result = manager.ValidateEquipment(entry, slot);
		REQUIRE(result.IsFailure());
		REQUIRE(result.GetError() == inventory_change_failure::CantDualWield);
	}

	SECTION("Allows weapon in offhand with dual wield")
	{
		context.SetCanDualWield(true);

		const auto entry = ItemEntryBuilder()
			.WithClass(item_class::Weapon)
			.WithInventoryType(inventory_type::OffHandWeapon)
			.Build();

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Offhand
		);

		const auto result = manager.ValidateEquipment(entry, slot);
		REQUIRE(result.IsSuccess());
	}
}

TEST_CASE("EquipmentManager - Two-handed weapon validation", "[equipment_manager]")
{
	MockEquipmentManagerContext context;
	EquipmentManager manager(context);

	SECTION("Rejects offhand item when mainhand has two-handed weapon")
	{
		// Setup: Two-handed weapon in mainhand
		const auto twoHandEntry = ItemEntryBuilder()
			.WithClass(item_class::Weapon)
			.WithInventoryType(inventory_type::TwoHandedWeapon)
			.Build();

		auto twoHandWeapon = CreateMockItem(twoHandEntry);

		const auto mainhandSlot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Mainhand
		);
		context.SetItemAtSlot(mainhandSlot.GetAbsolute(), twoHandWeapon);

		// Try to equip shield in offhand
		const auto shieldEntry = ItemEntryBuilder()
			.WithClass(item_class::Armor)
			.WithInventoryType(inventory_type::Shield)
			.Build();

		const auto offhandSlot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Offhand
		);

		const auto result = manager.ValidateEquipment(shieldEntry, offhandSlot);
		REQUIRE(result.IsFailure());
		REQUIRE(result.GetError() == inventory_change_failure::CantEquipWithTwoHanded);
	}

	SECTION("Allows offhand item when mainhand has one-handed weapon")
	{
		// Setup: One-handed weapon in mainhand
		const auto oneHandEntry = ItemEntryBuilder()
			.WithClass(item_class::Weapon)
			.WithInventoryType(inventory_type::MainHandWeapon)
			.Build();

		auto oneHandWeapon = CreateMockItem(oneHandEntry);

		const auto mainhandSlot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Mainhand
		);
		context.SetItemAtSlot(mainhandSlot.GetAbsolute(), oneHandWeapon);

		// Try to equip shield in offhand
		const auto shieldEntry = ItemEntryBuilder()
			.WithClass(item_class::Armor)
			.WithInventoryType(inventory_type::Shield)
			.Build();

		const auto offhandSlot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Offhand
		);

		const auto result = manager.ValidateEquipment(shieldEntry, offhandSlot);
		REQUIRE(result.IsSuccess());
	}
}

TEST_CASE("EquipmentManager - Apply equipment effects", "[equipment_manager]")
{
	MockEquipmentManagerContext context;
	EquipmentManager manager(context);

	SECTION("Applies stats when equipping new item")
	{
		const auto entry = ItemEntryBuilder()
			.WithInventoryType(inventory_type::Head)
			.Build();

		auto item = CreateMockItem(entry);

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Head
		);

		manager.ApplyEquipmentEffects(item, nullptr, slot);

		REQUIRE(context.WasStatsApplied());
		REQUIRE(context.GetLastVisualSlot() == player_equipment_slots::Head);
		REQUIRE(context.GetLastVisualEntryId() == entry.id());
	}

	SECTION("Removes old item stats when replacing")
	{
		const auto oldEntry = ItemEntryBuilder()
			.WithId(1000)
			.WithInventoryType(inventory_type::Head)
			.Build();

		const auto newEntry = ItemEntryBuilder()
			.WithId(2000)
			.WithInventoryType(inventory_type::Head)
			.Build();

		auto oldItem = CreateMockItem(oldEntry);
		auto newItem = CreateMockItem(newEntry);

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Head
		);

		manager.ApplyEquipmentEffects(newItem, oldItem, slot);

		REQUIRE(context.WasStatsApplied());
		REQUIRE(context.GetLastVisualEntryId() == newEntry.id());
	}

	SECTION("Applies Bind-on-Equip binding")
	{
		const auto entry = ItemEntryBuilder()
			.WithInventoryType(inventory_type::Chest)
			.WithBinding(item_binding::BindWhenEquipped)
			.Build();

		auto item = CreateMockItem(entry);

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Chest
		);

		manager.ApplyEquipmentEffects(item, nullptr, slot);

		const uint32 flags = item->Get<uint32>(object_fields::ItemFlags);
		REQUIRE((flags & item_flags::Bound) != 0);
	}

	SECTION("Does not bind items without Bind-on-Equip")
	{
		const auto entry = ItemEntryBuilder()
			.WithInventoryType(inventory_type::Chest)
			.WithBinding(item_binding::NoBinding)
			.Build();

		auto item = CreateMockItem(entry);

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Chest
		);

		manager.ApplyEquipmentEffects(item, nullptr, slot);

		const uint32 flags = item->Get<uint32>(object_fields::ItemFlags);
		REQUIRE((flags & item_flags::Bound) == 0);
	}

	SECTION("Handles item set effects")
	{
		const auto entry = ItemEntryBuilder()
			.WithInventoryType(inventory_type::Head)
			.WithItemSet(100)
			.Build();

		auto item = CreateMockItem(entry);

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Head
		);

		manager.ApplyEquipmentEffects(item, nullptr, slot);

		REQUIRE(context.WasItemSetEquipped());
	}
}

TEST_CASE("EquipmentManager - Remove equipment effects", "[equipment_manager]")
{
	MockEquipmentManagerContext context;
	EquipmentManager manager(context);

	SECTION("Removes stats when unequipping")
	{
		const auto entry = ItemEntryBuilder()
			.WithInventoryType(inventory_type::Chest)
			.Build();

		auto item = CreateMockItem(entry);

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Chest
		);

		manager.RemoveEquipmentEffects(item, slot);

		REQUIRE_FALSE(context.WasStatsApplied());
		REQUIRE(context.GetLastVisualSlot() == player_equipment_slots::Chest);
		REQUIRE(context.GetLastVisualEntryId() == 0);  // Cleared
	}

	SECTION("Removes item set effects when unequipping")
	{
		const auto entry = ItemEntryBuilder()
			.WithInventoryType(inventory_type::Head)
			.WithItemSet(200)
			.Build();

		auto item = CreateMockItem(entry);

		const auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Head
		);

		manager.RemoveEquipmentEffects(item, slot);

		REQUIRE_FALSE(context.WasItemSetEquipped());
	}
}
