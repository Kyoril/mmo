// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/item_validator.h"
#include "game_server/inventory_types.h"
#include "game_server/i_player_validator_context.h"
#include "game_server/objects/game_item_s.h"
#include "shared/proto_data/items.pb.h"

#include "catch.hpp"

using namespace mmo;

namespace
{
	/**
	 * @brief Mock implementation of IPlayerValidatorContext for testing.
	 * 
	 * Provides controlled player state for comprehensive unit testing
	 * without requiring full game world setup. Implements the interface
	 * contract properly, ensuring type safety and correct behavior.
	 */
	class MockPlayerValidatorContext final : public IPlayerValidatorContext
	{
	public:
		MockPlayerValidatorContext()
			: m_level(1)
			, m_weaponProficiency(0)
			, m_armorProficiency(0)
			, m_isAlive(true)
			, m_isInCombat(false)
			, m_canDualWield(false)
		{
		}

		// Setters for test setup
		void SetLevel(uint32 level)
		{
			m_level = level;
		}

		void SetWeaponProficiency(uint32 proficiency)
		{
			m_weaponProficiency = proficiency;
		}

		void SetArmorProficiency(uint32 proficiency)
		{
			m_armorProficiency = proficiency;
		}

		void SetAlive(bool alive)
		{
			m_isAlive = alive;
		}

		void SetInCombat(bool inCombat)
		{
			m_isInCombat = inCombat;
		}

		void SetCanDualWield(bool canDualWield)
		{
			m_canDualWield = canDualWield;
		}

		// IPlayerValidatorContext implementation
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

		bool IsAlive() const noexcept override
		{
			return m_isAlive;
		}

		bool IsInCombat() const noexcept override
		{
			return m_isInCombat;
		}

		bool CanDualWield() const noexcept override
		{
			return m_canDualWield;
		}

	private:
		uint32 m_level;
		uint32 m_weaponProficiency;
		uint32 m_armorProficiency;
		bool m_isAlive;
		bool m_isInCombat;
		bool m_canDualWield;
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
			m_entry.set_itemclass(item_class::Weapon);
			m_entry.set_subclass(item_subclass_weapon::OneHandedSword);
			m_entry.set_inventorytype(inventory_type::Weapon);
			m_entry.set_requiredlevel(0);
			m_entry.set_maxcount(0);
			m_entry.set_maxstack(1);
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

		ItemEntryBuilder& WithMaxCount(uint32 maxCount)
		{
			m_entry.set_maxcount(maxCount);
			return *this;
		}

		ItemEntryBuilder& WithMaxStack(uint32 maxStack)
		{
			m_entry.set_maxstack(maxStack);
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

TEST_CASE("ItemValidator - ValidateItemRequirements", "[item_validator]")
{
	MockPlayerValidatorContext player;
	ItemValidator validator(player);

	SECTION("Accepts items when all requirements are met")
	{
		player.SetLevel(10);
		player.SetWeaponProficiency(weapon_prof::OneHandSword);

		auto entry = ItemEntryBuilder()
			.WithClass(item_class::Weapon)
			.WithSubclass(item_subclass_weapon::OneHandedSword)
			.WithRequiredLevel(5)
			.Build();

		auto result = validator.ValidateItemRequirements(entry);
		REQUIRE(result.IsSuccess());
	}

	SECTION("Rejects items with insufficient level")
	{
		player.SetLevel(5);

		auto entry = ItemEntryBuilder()
			.WithRequiredLevel(10)
			.Build();

		auto result = validator.ValidateItemRequirements(entry);
		REQUIRE(result.IsFailure());
		REQUIRE(result.GetError() == inventory_change_failure::CantEquipLevel);
	}

	SECTION("Rejects weapons without proficiency")
	{
		player.SetLevel(10);
		player.SetWeaponProficiency(weapon_prof::OneHandSword);

		auto entry = ItemEntryBuilder()
			.WithClass(item_class::Weapon)
			.WithSubclass(item_subclass_weapon::TwoHandedAxe)
			.Build();

		auto result = validator.ValidateItemRequirements(entry);
		REQUIRE(result.IsFailure());
		REQUIRE(result.GetError() == inventory_change_failure::NoRequiredProficiency);
	}

	SECTION("Accepts weapons with correct proficiency")
	{
		player.SetWeaponProficiency(weapon_prof::OneHandSword | weapon_prof::TwoHandAxe);

		auto sword = ItemEntryBuilder()
			.WithClass(item_class::Weapon)
			.WithSubclass(item_subclass_weapon::OneHandedSword)
			.Build();

		REQUIRE(validator.ValidateItemRequirements(sword).IsSuccess());

		auto axe = ItemEntryBuilder()
			.WithClass(item_class::Weapon)
			.WithSubclass(item_subclass_weapon::TwoHandedAxe)
			.Build();

		REQUIRE(validator.ValidateItemRequirements(axe).IsSuccess());
	}

	SECTION("Rejects armor without proficiency")
	{
		player.SetArmorProficiency(armor_prof::Cloth);

		auto entry = ItemEntryBuilder()
			.WithClass(item_class::Armor)
			.WithSubclass(item_subclass_armor::Plate)
			.Build();

		auto result = validator.ValidateItemRequirements(entry);
		REQUIRE(result.IsFailure());
		REQUIRE(result.GetError() == inventory_change_failure::NoRequiredProficiency);
	}

	SECTION("Accepts armor with correct proficiency")
	{
		player.SetArmorProficiency(armor_prof::Cloth | armor_prof::Leather | armor_prof::Plate);

		auto cloth = ItemEntryBuilder()
			.WithClass(item_class::Armor)
			.WithSubclass(item_subclass_armor::Cloth)
			.Build();

		REQUIRE(validator.ValidateItemRequirements(cloth).IsSuccess());

		auto plate = ItemEntryBuilder()
			.WithClass(item_class::Armor)
			.WithSubclass(item_subclass_armor::Plate)
			.Build();

		REQUIRE(validator.ValidateItemRequirements(plate).IsSuccess());
	}

	SECTION("Accepts consumables without special requirements")
	{
		player.SetLevel(1);

		auto entry = ItemEntryBuilder()
			.WithClass(item_class::Consumable)
			.WithSubclass(item_subclass_consumable::Potion)
			.Build();

		auto result = validator.ValidateItemRequirements(entry);
		REQUIRE(result.IsSuccess());
	}
}

TEST_CASE("ItemValidator - ValidateItemLimits", "[item_validator]")
{
	MockPlayerValidatorContext player;
	ItemValidator validator(player);

	SECTION("Accepts items within limits")
	{
		auto entry = ItemEntryBuilder()
			.WithMaxCount(5)
			.WithMaxStack(20)
			.Build();

		auto result = validator.ValidateItemLimits(entry, 3, 0, 10);
		REQUIRE(result.IsSuccess());
	}

	SECTION("Rejects items exceeding max count")
	{
		auto entry = ItemEntryBuilder()
			.WithMaxCount(5)
			.Build();

		auto result = validator.ValidateItemLimits(entry, 3, 4, 10);
		REQUIRE(result.IsFailure());
		REQUIRE(result.GetError() == inventory_change_failure::CantCarryMoreOfThis);
	}

	SECTION("Rejects items when inventory is full")
	{
		auto entry = ItemEntryBuilder()
			.WithMaxStack(1)
			.Build();

		// Need 5 slots, only have 3
		auto result = validator.ValidateItemLimits(entry, 5, 0, 3);
		REQUIRE(result.IsFailure());
		REQUIRE(result.GetError() == inventory_change_failure::InventoryFull);
	}

	SECTION("Accepts stackable items with sufficient space")
	{
		auto entry = ItemEntryBuilder()
			.WithMaxStack(20)
			.Build();

		// 100 items needing 5 slots (100/20 = 5), we have 10 free
		auto result = validator.ValidateItemLimits(entry, 100, 0, 10);
		REQUIRE(result.IsSuccess());
	}

	SECTION("Handles items with no max count limit")
	{
		auto entry = ItemEntryBuilder()
			.WithMaxCount(0)
			.WithMaxStack(20)
			.Build();

		// No limit, should succeed even with high amounts
		auto result = validator.ValidateItemLimits(entry, 1000, 500, 100);
		REQUIRE(result.IsSuccess());
	}
}

TEST_CASE("ItemValidator - ValidatePlayerState", "[item_validator]")
{
	MockPlayerValidatorContext player;
	ItemValidator validator(player);

	SECTION("Accepts operations when player is alive and not in combat")
	{
		player.SetAlive(true);
		player.SetInCombat(false);

		auto result = validator.ValidatePlayerState(false);
		REQUIRE(result.IsSuccess());
	}

	SECTION("Rejects operations when player is dead")
	{
		player.SetAlive(false);

		auto result = validator.ValidatePlayerState(false);
		REQUIRE(result.IsFailure());
		REQUIRE(result.GetError() == inventory_change_failure::YouAreDead);
	}

	SECTION("Rejects equipment changes while in combat")
	{
		player.SetAlive(true);
		player.SetInCombat(true);

		auto result = validator.ValidatePlayerState(true);
		REQUIRE(result.IsFailure());
		REQUIRE(result.GetError() == inventory_change_failure::NotInCombat);
	}

	SECTION("Allows non-equipment operations while in combat")
	{
		player.SetAlive(true);
		player.SetInCombat(true);

		auto result = validator.ValidatePlayerState(false);
		REQUIRE(result.IsSuccess());
	}
}

TEST_CASE("ItemValidator - ValidateSlotPlacement for equipment", "[item_validator]")
{
	MockPlayerValidatorContext player;
	player.SetLevel(10);
	player.SetWeaponProficiency(weapon_prof::OneHandSword);
	player.SetArmorProficiency(armor_prof::Cloth);
	ItemValidator validator(player);

	SECTION("Accepts head items in head slot")
	{
		auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Head
		);

		auto entry = ItemEntryBuilder()
			.WithClass(item_class::Armor)
			.WithInventoryType(inventory_type::Head)
			.WithSubclass(item_subclass_armor::Cloth)
			.Build();

		auto result = validator.ValidateSlotPlacement(slot, entry);
		REQUIRE(result.IsSuccess());
	}

	SECTION("Rejects weapons in head slot")
	{
		auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Head
		);

		auto entry = ItemEntryBuilder()
			.WithClass(item_class::Weapon)
			.WithInventoryType(inventory_type::Weapon)
			.Build();

		auto result = validator.ValidateSlotPlacement(slot, entry);
		REQUIRE(result.IsFailure());
		REQUIRE(result.GetError() == inventory_change_failure::ItemDoesNotGoToSlot);
	}

	SECTION("Accepts chest or robe items in chest slot")
	{
		auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Chest
		);

		auto chest = ItemEntryBuilder()
			.WithClass(item_class::Armor)
			.WithInventoryType(inventory_type::Chest)
			.WithSubclass(item_subclass_armor::Cloth)
			.Build();

		REQUIRE(validator.ValidateSlotPlacement(slot, chest).IsSuccess());

		auto robe = ItemEntryBuilder()
			.WithClass(item_class::Armor)
			.WithInventoryType(inventory_type::Robe)
			.WithSubclass(item_subclass_armor::Cloth)
			.Build();

		REQUIRE(validator.ValidateSlotPlacement(slot, robe).IsSuccess());
	}

	SECTION("Accepts rings in finger slots")
	{
		player.SetArmorProficiency(armor_prof::Common);  // Rings are misc armor

		auto slot1 = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Finger1
		);

		auto slot2 = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Finger2
		);

		auto entry = ItemEntryBuilder()
			.WithClass(item_class::Armor)
			.WithInventoryType(inventory_type::Finger)
			.WithSubclass(item_subclass_armor::Misc)
			.Build();

		REQUIRE(validator.ValidateSlotPlacement(slot1, entry).IsSuccess());
		REQUIRE(validator.ValidateSlotPlacement(slot2, entry).IsSuccess());
	}

	SECTION("Accepts trinkets in trinket slots")
	{
		player.SetArmorProficiency(armor_prof::Common);  // Trinkets are misc armor

		auto slot1 = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Trinket1
		);

		auto slot2 = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Trinket2
		);

		auto entry = ItemEntryBuilder()
			.WithClass(item_class::Armor)
			.WithInventoryType(inventory_type::Trinket)
			.WithSubclass(item_subclass_armor::Misc)
			.Build();

		REQUIRE(validator.ValidateSlotPlacement(slot1, entry).IsSuccess());
		REQUIRE(validator.ValidateSlotPlacement(slot2, entry).IsSuccess());
	}
}

TEST_CASE("ItemValidator - ValidateSlotPlacement for weapons", "[item_validator]")
{
	MockPlayerValidatorContext player;
	player.SetLevel(10);
	player.SetWeaponProficiency(weapon_prof::OneHandSword | weapon_prof::TwoHandSword);
	player.SetCanDualWield(false);
	ItemValidator validator(player);

	SECTION("Accepts weapons in mainhand slot")
	{
		auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Mainhand
		);

		auto entry = ItemEntryBuilder()
			.WithClass(item_class::Weapon)
			.WithSubclass(item_subclass_weapon::OneHandedSword)
			.WithInventoryType(inventory_type::Weapon)
			.Build();

		auto result = validator.ValidateSlotPlacement(slot, entry);
		REQUIRE(result.IsSuccess());
	}

	SECTION("Accepts two-handed weapons in mainhand slot")
	{
		auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Mainhand
		);

		auto entry = ItemEntryBuilder()
			.WithClass(item_class::Weapon)
			.WithSubclass(item_subclass_weapon::TwoHandedSword)
			.WithInventoryType(inventory_type::TwoHandedWeapon)
			.Build();

		auto result = validator.ValidateSlotPlacement(slot, entry);
		REQUIRE(result.IsSuccess());
	}

	SECTION("Rejects offhand weapons without dual wield")
	{
		auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Offhand
		);

		auto entry = ItemEntryBuilder()
			.WithClass(item_class::Weapon)
			.WithSubclass(item_subclass_weapon::OneHandedSword)
			.WithInventoryType(inventory_type::Weapon)
			.Build();

		auto result = validator.ValidateSlotPlacement(slot, entry);
		REQUIRE(result.IsFailure());
		REQUIRE(result.GetError() == inventory_change_failure::CantDualWield);
	}

	SECTION("Accepts offhand weapons with dual wield")
	{
		player.SetCanDualWield(true);

		auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Offhand
		);

		auto entry = ItemEntryBuilder()
			.WithClass(item_class::Weapon)
			.WithSubclass(item_subclass_weapon::OneHandedSword)
			.WithInventoryType(inventory_type::Weapon)
			.Build();

		auto result = validator.ValidateSlotPlacement(slot, entry);
		REQUIRE(result.IsSuccess());
	}

	SECTION("Accepts shields in offhand without dual wield")
	{
		player.SetArmorProficiency(armor_prof::Shield);  // Shields require shield proficiency

		auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Offhand
		);

		auto entry = ItemEntryBuilder()
			.WithClass(item_class::Armor)
			.WithSubclass(item_subclass_armor::Shield)
			.WithInventoryType(inventory_type::Shield)
			.Build();

		auto result = validator.ValidateSlotPlacement(slot, entry);
		REQUIRE(result.IsSuccess());
	}

	SECTION("Accepts holdables in offhand without dual wield")
	{
		player.SetArmorProficiency(armor_prof::Common);  // Holdables are misc armor

		auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_equipment_slots::Offhand
		);

		auto entry = ItemEntryBuilder()
			.WithClass(item_class::Armor)
			.WithSubclass(item_subclass_armor::Misc)
			.WithInventoryType(inventory_type::Holdable)
			.Build();

		auto result = validator.ValidateSlotPlacement(slot, entry);
		REQUIRE(result.IsSuccess());
	}
}

TEST_CASE("ItemValidator - ValidateSlotPlacement for bags", "[item_validator]")
{
	MockPlayerValidatorContext player;
	ItemValidator validator(player);

	SECTION("Accepts bags in bag pack slots")
	{
		auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_slots::Start
		);

		auto entry = ItemEntryBuilder()
			.WithClass(item_class::Container)
			.WithInventoryType(inventory_type::Bag)
			.Build();

		auto result = validator.ValidateSlotPlacement(slot, entry);
		REQUIRE(result.IsSuccess());
	}

	SECTION("Accepts quivers in bag pack slots")
	{
		auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_slots::Start
		);

		auto entry = ItemEntryBuilder()
			.WithClass(item_class::Quiver)
			.WithInventoryType(inventory_type::Quiver)
			.Build();

		auto result = validator.ValidateSlotPlacement(slot, entry);
		REQUIRE(result.IsSuccess());
	}

	SECTION("Rejects non-bags in bag pack slots")
	{
		auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_slots::Start
		);

		auto entry = ItemEntryBuilder()
			.WithClass(item_class::Weapon)
			.WithInventoryType(inventory_type::Weapon)
			.Build();

		auto result = validator.ValidateSlotPlacement(slot, entry);
		REQUIRE(result.IsFailure());
		REQUIRE(result.GetError() == inventory_change_failure::NotABag);
	}

	SECTION("Accepts any item in inventory slots")
	{
		auto slot = InventorySlot::FromRelative(
			player_inventory_slots::Bag_0,
			player_inventory_pack_slots::Start
		);

		auto weapon = ItemEntryBuilder()
			.WithClass(item_class::Weapon)
			.Build();

		REQUIRE(validator.ValidateSlotPlacement(slot, weapon).IsSuccess());

		auto consumable = ItemEntryBuilder()
			.WithClass(item_class::Consumable)
			.Build();

		REQUIRE(validator.ValidateSlotPlacement(slot, consumable).IsSuccess());

		auto bag = ItemEntryBuilder()
			.WithClass(item_class::Container)
			.Build();

		REQUIRE(validator.ValidateSlotPlacement(slot, bag).IsSuccess());
	}
}

TEST_CASE("ItemValidator - Edge cases", "[item_validator]")
{
	MockPlayerValidatorContext player;
	player.SetLevel(10);
	ItemValidator validator(player);

	SECTION("Handles unknown slot types")
	{
		// Create a slot that doesn't match any known type
		auto unknownSlot = InventorySlot::FromAbsolute(0xFFFF);

		auto entry = ItemEntryBuilder().Build();

		auto result = validator.ValidateSlotPlacement(unknownSlot, entry);
		REQUIRE(result.IsFailure());
		REQUIRE(result.GetError() == inventory_change_failure::InternalBagError);
	}

	SECTION("Handles items with zero required level")
	{
		player.SetLevel(1);
		player.SetWeaponProficiency(weapon_prof::OneHandSword);  // Default test weapon needs proficiency

		auto entry = ItemEntryBuilder()
			.WithRequiredLevel(0)
			.Build();

		auto result = validator.ValidateItemRequirements(entry);
		REQUIRE(result.IsSuccess());
	}

	SECTION("Handles exact level requirement match")
	{
		player.SetLevel(10);
		player.SetWeaponProficiency(weapon_prof::OneHandSword);  // Default test weapon needs proficiency

		auto entry = ItemEntryBuilder()
			.WithRequiredLevel(10)
			.Build();

		auto result = validator.ValidateItemRequirements(entry);
		REQUIRE(result.IsSuccess());
	}
}
