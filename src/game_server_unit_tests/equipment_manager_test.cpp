// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/equipment_manager.h"
#include "game_server/inventory_types.h"
#include "game_server/objects/game_item_s.h"
#include "shared/proto_data/items.pb.h"
#include "shared/proto_data/project.h"

#include "catch.hpp"

#include <map>
#include <memory>
#include <set>

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
			, m_canDualWield(true)
			, m_statsApplied(false)
			, m_itemSetEquipped(false)
		{
			// Add all proficiencies by default for tests that don't care about proficiency
			for (uint32 i = 1; i <= 32; ++i)
			{
				m_proficiencies.insert(i);
			}
		}

		// Test setup methods
		void SetLevel(uint32 level) { m_level = level; }
		void SetCanDualWield(bool canDualWield) { m_canDualWield = canDualWield; }
		void SetProject(proto::Project* project) { m_project = project; }

		void AddProficiency(uint32 proficiencyId)
		{
			if (proficiencyId > 0)
			{
				m_proficiencies.insert(proficiencyId);
			}
		}

		void RemoveProficiency(uint32 proficiencyId)
		{
			m_proficiencies.erase(proficiencyId);
		}

		void ClearProficiencies()
		{
			m_proficiencies.clear();
		}

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

		bool HasProficiency(uint32 proficiencyId) const noexcept override
		{
			// Proficiency ID 0 means no proficiency required
			if (proficiencyId == 0)
			{
				return true;
			}

			return m_proficiencies.contains(proficiencyId);
		}

		const proto::Project& GetProject() const noexcept override
		{
			ASSERT(m_project != nullptr);
			return *m_project;
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
		bool m_canDualWield;
		std::set<uint32> m_proficiencies;
		std::map<uint16, std::shared_ptr<GameItemS>> m_items;
		proto::Project* m_project { nullptr };

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

	/**
	 * @brief Gets a shared test project for use in tests.
	 */
	proto::Project& GetTestProject()
	{
		static proto::Project testProject;
		return testProject;
	}
}

TEST_CASE("EquipmentManager - Slot compatibility validation", "[equipment_manager]")
{
	MockEquipmentManagerContext context;
	context.SetProject(&GetTestProject());
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
	context.SetProject(&GetTestProject());
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
	context.SetProject(&GetTestProject());
	EquipmentManager manager(context);

	// Set up subclasses with proficiency IDs for the new data-driven system
	// We use subclass IDs starting from 1 to avoid ID 0 issues
	auto& project = GetTestProject();

	// Define test subclass and proficiency IDs
	constexpr uint32 OneHandedAxeSubclass = 1;
	constexpr uint32 TwoHandedAxeSubclass = 2;
	constexpr uint32 ClothSubclass = 3;
	constexpr uint32 LeatherSubclass = 4;
	constexpr uint32 OneHandedAxeProficiency = 1;
	constexpr uint32 TwoHandedAxeProficiency = 2;
	constexpr uint32 ClothProficiency = 3;
	constexpr uint32 LeatherProficiency = 4;

	// Add subclasses if they don't exist yet
	if (!project.itemSubclasses.getById(OneHandedAxeSubclass))
	{
		auto* axe1h = project.itemSubclasses.add(OneHandedAxeSubclass);
		if (axe1h)
		{
			axe1h->set_requiredproficiency(OneHandedAxeProficiency);
		}
	}

	if (!project.itemSubclasses.getById(TwoHandedAxeSubclass))
	{
		auto* axe2h = project.itemSubclasses.add(TwoHandedAxeSubclass);
		if (axe2h)
		{
			axe2h->set_requiredproficiency(TwoHandedAxeProficiency);
		}
	}

	if (!project.itemSubclasses.getById(ClothSubclass))
	{
		auto* cloth = project.itemSubclasses.add(ClothSubclass);
		if (cloth)
		{
			cloth->set_requiredproficiency(ClothProficiency);
		}
	}

	if (!project.itemSubclasses.getById(LeatherSubclass))
	{
		auto* leather = project.itemSubclasses.add(LeatherSubclass);
		if (leather)
		{
			leather->set_requiredproficiency(LeatherProficiency);
		}
	}

	SECTION("Validates weapon proficiency")
	{
		// Clear all proficiencies and add only the one we want
		context.ClearProficiencies();
		context.AddProficiency(OneHandedAxeProficiency);  // Only has proficiency 1

		const auto validEntry = ItemEntryBuilder()
			.WithClass(item_class::Weapon)
			.WithSubclass(OneHandedAxeSubclass)  // Has proficiency
			.WithInventoryType(inventory_type::MainHandWeapon)
			.Build();

		const auto invalidEntry = ItemEntryBuilder()
			.WithClass(item_class::Weapon)
			.WithSubclass(TwoHandedAxeSubclass)  // No proficiency
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
		// Clear all proficiencies and add only the one we want
		context.ClearProficiencies();
		context.AddProficiency(ClothProficiency);  // Only has proficiency 3

		const auto validEntry = ItemEntryBuilder()
			.WithClass(item_class::Armor)
			.WithSubclass(ClothSubclass)  // Has proficiency
			.WithInventoryType(inventory_type::Chest)
			.Build();

		const auto invalidEntry = ItemEntryBuilder()
			.WithClass(item_class::Armor)
			.WithSubclass(LeatherSubclass)  // No proficiency
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
	context.SetProject(&GetTestProject());
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
	context.SetProject(&GetTestProject());
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
	context.SetProject(&GetTestProject());
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
	context.SetProject(&GetTestProject());
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
	context.SetProject(&GetTestProject());
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
