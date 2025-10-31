// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "item_validator.h"

#include "game_server/objects/game_item_s.h"
#include "game_server/objects/game_bag_s.h"
#include "game_server/objects/game_player_s.h"
#include "shared/proto_data/items.pb.h"

namespace mmo
{
	ItemValidator::ItemValidator(const GamePlayerS& player)
		: m_player(player)
	{
	}

	InventoryResult<void> ItemValidator::ValidateItemRequirements(
		const proto::ItemEntry& entry) const
	{
		// Check level requirement
		if (entry.requiredlevel() > 0 && entry.requiredlevel() > m_player.GetLevel())
		{
			return InventoryResult<void>::Failure(inventory_change_failure::CantEquipLevel);
		}

		// Check skill requirement
		// TODO: Uncomment when skill system is available
		/*
		if (entry.requiredskill() != 0 && !m_player.HasSkill(entry.requiredskill()))
		{
			return InventoryResult<void>::Failure(inventory_change_failure::CantEquipSkill);
		}
		*/

		// Check proficiency for weapons
		if (entry.itemclass() == item_class::Weapon)
		{
			if (!HasWeaponProficiency(entry))
			{
				return InventoryResult<void>::Failure(inventory_change_failure::NoRequiredProficiency);
			}
		}

		// Check proficiency for armor
		if (entry.itemclass() == item_class::Armor)
		{
			if (!HasArmorProficiency(entry))
			{
				return InventoryResult<void>::Failure(inventory_change_failure::NoRequiredProficiency);
			}
		}

		return InventoryResult<void>::Success();
	}

	InventoryResult<void> ItemValidator::ValidateSlotPlacement(
		InventorySlot slot,
		const proto::ItemEntry& entry) const
	{
		// Validate based on slot type
		if (slot.IsEquipment())
		{
			return ValidateEquipmentSlot(slot, entry);
		}

		if (slot.IsBagPack())
		{
			return ValidateBagPackSlot(slot, entry);
		}

		if (slot.IsBag())
		{
			return ValidateBagSlot(slot, entry);
		}

		if (slot.IsInventory())
		{
			// Inventory slots accept any non-bag items
			return InventoryResult<void>::Success();
		}

		// Unknown slot type
		return InventoryResult<void>::Failure(inventory_change_failure::InternalBagError);
	}

	InventoryResult<void> ItemValidator::ValidateItemLimits(
		const proto::ItemEntry& entry,
		uint16 amount,
		uint16 currentCount,
		uint16 freeSlots) const
	{
		// Check max count per item
		if (entry.maxcount() > 0)
		{
			if (static_cast<uint32>(currentCount + amount) > entry.maxcount())
			{
				return InventoryResult<void>::Failure(inventory_change_failure::CantCarryMoreOfThis);
			}
		}

		// Quick check if there are enough free slots
		// (only works if we don't have an item of this type yet)
		const uint16 requiredSlots = (amount - 1) / entry.maxstack() + 1;
		if ((currentCount == 0 || entry.maxstack() <= 1) && requiredSlots > freeSlots)
		{
			return InventoryResult<void>::Failure(inventory_change_failure::InventoryFull);
		}

		return InventoryResult<void>::Success();
	}

	InventoryResult<void> ItemValidator::ValidatePlayerState(
		bool isEquipmentChange) const
	{
		// Check if player is alive
		if (!m_player.IsAlive())
		{
			return InventoryResult<void>::Failure(inventory_change_failure::YouAreDead);
		}

		// Can't change equipment while in combat (except weapons)
		if (isEquipmentChange && m_player.IsInCombat())
		{
			return InventoryResult<void>::Failure(inventory_change_failure::NotInCombat);
		}

		return InventoryResult<void>::Success();
	}

	InventoryResult<void> ItemValidator::ValidateSwap(
		InventorySlot slotA,
		InventorySlot slotB,
		const GameItemS* itemA,
		const GameItemS* itemB) const
	{
		if (!itemA)
		{
			return InventoryResult<void>::Failure(inventory_change_failure::ItemNotFound);
		}

		// Validate state
		auto stateResult = ValidatePlayerState(slotA.IsEquipment() || slotB.IsEquipment());
		if (stateResult.IsFailure())
		{
			return stateResult;
		}

		// If source item is a bag, it must be empty
		if (itemA->IsContainer())
		{
			auto bag = static_cast<const GameBagS*>(itemA);
			if (!bag->IsEmpty())
			{
				return InventoryResult<void>::Failure(inventory_change_failure::CanOnlyDoWithEmptyBags);
			}
		}

		// Destination bag must be empty as well
		if (itemB && itemB->IsContainer())
		{
			auto bag = static_cast<const GameBagS*>(itemB);
			if (!bag->IsEmpty())
			{
				return InventoryResult<void>::Failure(inventory_change_failure::CanOnlyDoWithEmptyBags);
			}
		}

		// Verify destination slot for source item
		auto result = ValidateSlotPlacement(slotB, itemA->GetEntry());
		if (result.IsFailure())
		{
			return result;
		}

		// If there is an item in the destination slot, also verify the source slot
		if (itemB)
		{
			result = ValidateSlotPlacement(slotA, itemB->GetEntry());
			if (result.IsFailure())
			{
				return result;
			}
		}

		return InventoryResult<void>::Success();
	}

	bool ItemValidator::HasWeaponProficiency(const proto::ItemEntry& entry) const
	{
		const weapon_prof::Type requiredProf = GetWeaponProficiency(entry.subclass());
		const uint32 playerProf = m_player.GetWeaponProficiency();
		
		return (playerProf & (1 << requiredProf)) != 0;
	}

	bool ItemValidator::HasArmorProficiency(const proto::ItemEntry& entry) const
	{
		const armor_prof::Type requiredProf = GetArmorProficiency(entry.subclass());
		const uint32 playerProf = m_player.GetArmorProficiency();
		
		return (playerProf & (1 << requiredProf)) != 0;
	}

	InventoryResult<void> ItemValidator::ValidateEquipmentSlot(
		InventorySlot slot,
		const proto::ItemEntry& entry) const
	{
		// First validate item requirements
		auto requirementsResult = ValidateItemRequirements(entry);
		if (requirementsResult.IsFailure())
		{
			return requirementsResult;
		}

		const uint8 equipSlot = slot.GetSlot();
		const auto invType = entry.inventorytype();

		// Validate each equipment slot type
		switch (equipSlot)
		{
		case player_equipment_slots::Head:
			if (invType != inventory_type::Head)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Body:
			if (invType != inventory_type::Body)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Chest:
			if (invType != inventory_type::Chest && invType != inventory_type::Robe)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Feet:
			if (invType != inventory_type::Feet)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Neck:
			if (invType != inventory_type::Neck)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Ranged:
			if (invType != inventory_type::Ranged &&
				invType != inventory_type::Thrown &&
				invType != inventory_type::RangedRight)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Finger1:
		case player_equipment_slots::Finger2:
			if (invType != inventory_type::Finger)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Trinket1:
		case player_equipment_slots::Trinket2:
			if (invType != inventory_type::Trinket)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Hands:
			if (invType != inventory_type::Hands)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Legs:
			if (invType != inventory_type::Legs)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Mainhand:
			return ValidateTwoHandedWeapon(slot, entry);

		case player_equipment_slots::Offhand:
			return ValidateOffhandWeapon(slot, entry);

		case player_equipment_slots::Shoulders:
			if (invType != inventory_type::Shoulders)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Tabard:
			if (invType != inventory_type::Tabard)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Waist:
			if (invType != inventory_type::Waist)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Wrists:
			if (invType != inventory_type::Wrists)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Back:
			if (invType != inventory_type::Cloak)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		default:
			return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
		}

		return InventoryResult<void>::Success();
	}

	InventoryResult<void> ItemValidator::ValidateBagSlot(
		InventorySlot slot,
		const proto::ItemEntry& entry) const
	{
		// Bag slots inside equipped bags have special restrictions
		// TODO: Validate against bag type (e.g., quiver can only hold ammo)
		
		if (entry.inventorytype() == inventory_type::Ammo)
		{
			// Check if the bag is a quiver
			// This requires access to the actual bag instance
			// For now, we'll allow it and let the caller handle bag-specific validation
		}

		return InventoryResult<void>::Success();
	}

	InventoryResult<void> ItemValidator::ValidateBagPackSlot(
		InventorySlot slot,
		const proto::ItemEntry& entry) const
	{
		// Only bags and quivers can go in bag pack slots
		if (entry.itemclass() != item_class::Container &&
			entry.itemclass() != item_class::Quiver)
		{
			return InventoryResult<void>::Failure(inventory_change_failure::NotABag);
		}

		// Only one quiver can be equipped at a time
		// This requires knowledge of other equipped bags
		// Will be validated by the caller with access to full inventory state

		return InventoryResult<void>::Success();
	}

	InventoryResult<void> ItemValidator::ValidateTwoHandedWeapon(
		InventorySlot slot,
		const proto::ItemEntry& entry) const
	{
		const auto invType = entry.inventorytype();

		if (invType != inventory_type::MainHandWeapon &&
			invType != inventory_type::TwoHandedWeapon &&
			invType != inventory_type::Weapon)
		{
			return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
		}

		// Two-handed weapons require empty offhand
		// This will be validated by the caller with access to inventory state

		return InventoryResult<void>::Success();
	}

	InventoryResult<void> ItemValidator::ValidateOffhandWeapon(
		InventorySlot slot,
		const proto::ItemEntry& entry) const
	{
		const auto invType = entry.inventorytype();

		if (invType != inventory_type::OffHandWeapon &&
			invType != inventory_type::Shield &&
			invType != inventory_type::Weapon &&
			invType != inventory_type::Holdable)
		{
			return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
		}

		// Check dual wield capability for weapons (not shields/holdables)
		if (invType != inventory_type::Shield &&
			invType != inventory_type::Holdable &&
			!m_player.CanDualWield())
		{
			return InventoryResult<void>::Failure(inventory_change_failure::CantDualWield);
		}

		// Can't equip offhand if mainhand has 2H weapon
		// This will be validated by the caller with access to inventory state

		return InventoryResult<void>::Success();
	}

	weapon_prof::Type ItemValidator::GetWeaponProficiency(uint32 subclass)
	{
		switch (subclass)
		{
		case item_subclass_weapon::OneHandedAxe:
			return weapon_prof::OneHandAxe;
		case item_subclass_weapon::TwoHandedAxe:
			return weapon_prof::TwoHandAxe;
		case item_subclass_weapon::Bow:
			return weapon_prof::Bow;
		case item_subclass_weapon::CrossBow:
			return weapon_prof::Crossbow;
		case item_subclass_weapon::Dagger:
			return weapon_prof::Dagger;
		case item_subclass_weapon::Fist:
			return weapon_prof::Fist;
		case item_subclass_weapon::Gun:
			return weapon_prof::Gun;
		case item_subclass_weapon::OneHandedMace:
			return weapon_prof::OneHandMace;
		case item_subclass_weapon::TwoHandedMace:
			return weapon_prof::TwoHandMace;
		case item_subclass_weapon::Polearm:
			return weapon_prof::Polearm;
		case item_subclass_weapon::Staff:
			return weapon_prof::Staff;
		case item_subclass_weapon::OneHandedSword:
			return weapon_prof::OneHandSword;
		case item_subclass_weapon::TwoHandedSword:
			return weapon_prof::TwoHandSword;
		case item_subclass_weapon::Thrown:
			return weapon_prof::Throw;
		case item_subclass_weapon::Wand:
			return weapon_prof::Wand;
		}

		return weapon_prof::None;
	}

	armor_prof::Type ItemValidator::GetArmorProficiency(uint32 subclass)
	{
		switch (subclass)
		{
		case item_subclass_armor::Misc:
			return armor_prof::Common;
		case item_subclass_armor::Buckler:
		case item_subclass_armor::Shield:
			return armor_prof::Shield;
		case item_subclass_armor::Cloth:
			return armor_prof::Cloth;
		case item_subclass_armor::Leather:
			return armor_prof::Leather;
		case item_subclass_armor::Mail:
			return armor_prof::Mail;
		case item_subclass_armor::Plate:
			return armor_prof::Plate;
		}

		return armor_prof::None;
	}
}
