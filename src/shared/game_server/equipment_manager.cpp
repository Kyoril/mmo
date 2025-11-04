// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "equipment_manager.h"

#include "game_server/objects/game_item_s.h"
#include "shared/proto_data/items.pb.h"
#include "base/macros.h"

namespace mmo
{
	EquipmentManager::EquipmentManager(IEquipmentManagerContext& context) noexcept
		: m_context(context)
	{
	}

	InventoryResult<void> EquipmentManager::ValidateEquipment(
		const proto::ItemEntry& entry,
		InventorySlot slot) const noexcept
	{
		ASSERT(slot.IsEquipment());

		// Validate level requirement
		if (entry.requiredlevel() > 0 && entry.requiredlevel() > m_context.GetLevel())
		{
			return InventoryResult<void>::Failure(inventory_change_failure::CantEquipLevel);
		}

		// Validate proficiency
		if (auto result = ValidateProficiency(entry); result.IsFailure())
		{
			return result;
		}

		// Validate slot compatibility
		const auto inventoryType = static_cast<inventory_type::Type>(entry.inventorytype());
		if (auto result = ValidateSlotCompatibility(inventoryType, slot); result.IsFailure())
		{
			return result;
		}

		// Validate two-handed weapon constraints
		if (auto result = ValidateTwoHandedWeapon(inventoryType, slot); result.IsFailure())
		{
			return result;
		}

		// Validate offhand weapon constraints
		if (slot.GetSlot() == player_equipment_slots::Offhand)
		{
			if (auto result = ValidateOffhandWeapon(entry, slot); result.IsFailure())
			{
				return result;
			}
		}

		return InventoryResult<void>::Success();
	}

	void EquipmentManager::ApplyEquipmentEffects(
		std::shared_ptr<GameItemS> newItem,
		std::shared_ptr<GameItemS> oldItem,
		InventorySlot slot) noexcept
	{
		ASSERT(newItem);
		ASSERT(slot.IsEquipment());

		const uint8 equipSlot = slot.GetSlot();

		// Remove old item effects if replacing
		if (oldItem)
		{
			m_context.ApplyItemStats(*oldItem, false);

			if (oldItem->GetEntry().itemset() != 0)
			{
				m_context.HandleItemSetEffect(oldItem->GetEntry().itemset(), false);
			}
		}

		// Apply new item effects
		m_context.ApplyItemStats(*newItem, true);

		if (newItem->GetEntry().itemset() != 0)
		{
			m_context.HandleItemSetEffect(newItem->GetEntry().itemset(), true);
		}

		// Update visual
		m_context.UpdateEquipmentVisual(
			equipSlot,
			newItem->GetEntry().id(),
			newItem->Get<uint64>(object_fields::Creator)
		);

		// Apply Bind-on-Equip binding
		if (newItem->GetEntry().bonding() == item_binding::BindWhenEquipped)
		{
			newItem->AddFlag<uint32>(object_fields::ItemFlags, item_flags::Bound);
		}
	}

	void EquipmentManager::RemoveEquipmentEffects(
		std::shared_ptr<GameItemS> item,
		InventorySlot slot) noexcept
	{
		ASSERT(item);
		ASSERT(slot.IsEquipment());

		const uint8 equipSlot = slot.GetSlot();

		// Remove item stats
		m_context.ApplyItemStats(*item, false);

		// Remove item set effects
		if (item->GetEntry().itemset() != 0)
		{
			m_context.HandleItemSetEffect(item->GetEntry().itemset(), false);
		}

		// Clear visual (entry 0, creator 0)
		m_context.UpdateEquipmentVisual(equipSlot, 0, 0);
	}

	InventoryResult<void> EquipmentManager::ValidateSlotCompatibility(
		inventory_type::Type inventoryType,
		InventorySlot slot) const noexcept
	{
		const uint8 equipSlot = slot.GetSlot();

		switch (equipSlot)
		{
		case player_equipment_slots::Head:
			if (inventoryType != inventory_type::Head)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Neck:
			if (inventoryType != inventory_type::Neck)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Shoulders:
			if (inventoryType != inventory_type::Shoulders)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Body:
			if (inventoryType != inventory_type::Body)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Chest:
			if (inventoryType != inventory_type::Chest &&
				inventoryType != inventory_type::Robe)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Waist:
			if (inventoryType != inventory_type::Waist)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Legs:
			if (inventoryType != inventory_type::Legs)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Feet:
			if (inventoryType != inventory_type::Feet)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Wrists:
			if (inventoryType != inventory_type::Wrists)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Hands:
			if (inventoryType != inventory_type::Hands)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Finger1:
		case player_equipment_slots::Finger2:
			if (inventoryType != inventory_type::Finger)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Trinket1:
		case player_equipment_slots::Trinket2:
			if (inventoryType != inventory_type::Trinket)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Back:
			if (inventoryType != inventory_type::Cloak)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Mainhand:
			if (inventoryType != inventory_type::MainHandWeapon &&
				inventoryType != inventory_type::TwoHandedWeapon &&
				inventoryType != inventory_type::Weapon)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Offhand:
			if (inventoryType != inventory_type::OffHandWeapon &&
				inventoryType != inventory_type::Shield &&
				inventoryType != inventory_type::Weapon &&
				inventoryType != inventory_type::Holdable)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Ranged:
			if (inventoryType != inventory_type::Ranged &&
				inventoryType != inventory_type::Thrown &&
				inventoryType != inventory_type::RangedRight)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		case player_equipment_slots::Tabard:
			if (inventoryType != inventory_type::Tabard)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
			}
			break;

		default:
			// Unknown equipment slot
			return InventoryResult<void>::Failure(inventory_change_failure::ItemDoesNotGoToSlot);
		}

		return InventoryResult<void>::Success();
	}

	InventoryResult<void> EquipmentManager::ValidateProficiency(
		const proto::ItemEntry& entry) const noexcept
	{
		if (entry.itemclass() == item_class::Weapon)
		{
			const uint32 weaponProf = m_context.GetWeaponProficiency();
			const uint32 requiredProf = 1 << entry.subclass();

			if ((weaponProf & requiredProf) == 0)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::NoRequiredProficiency);
			}
		}
		else if (entry.itemclass() == item_class::Armor)
		{
			const uint32 armorProf = m_context.GetArmorProficiency();
			const uint32 requiredProf = (1 << entry.subclass());

			if ((armorProf & requiredProf) == 0)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::NoRequiredProficiency);
			}
		}

		return InventoryResult<void>::Success();
	}

	InventoryResult<void> EquipmentManager::ValidateTwoHandedWeapon(
		inventory_type::Type inventoryType,
		InventorySlot slot) const noexcept
	{
		const uint8 equipSlot = slot.GetSlot();

		if (equipSlot == player_equipment_slots::Mainhand &&
			inventoryType == inventory_type::TwoHandedWeapon)
		{
			// Check if offhand is occupied
			const auto offhandSlot = InventorySlot::FromRelative(
				player_inventory_slots::Bag_0,
				player_equipment_slots::Offhand
			);
			auto offhand = m_context.GetItemAtSlot(offhandSlot.GetAbsolute());

			if (offhand)
			{
				// For production use, would need to verify there's space to move the offhand
				// This simplified version just checks if offhand exists
				// Full implementation would require ISlotManagerContext integration
			}
		}
		else if (equipSlot == player_equipment_slots::Offhand)
		{
			// Check if mainhand has a two-handed weapon
			const auto mainhandSlot = InventorySlot::FromRelative(
				player_inventory_slots::Bag_0,
				player_equipment_slots::Mainhand
			);
			auto mainhand = m_context.GetItemAtSlot(mainhandSlot.GetAbsolute());

			if (mainhand &&
				mainhand->GetEntry().inventorytype() == inventory_type::TwoHandedWeapon)
			{
				return InventoryResult<void>::Failure(inventory_change_failure::CantEquipWithTwoHanded);
			}
		}

		return InventoryResult<void>::Success();
	}

	InventoryResult<void> EquipmentManager::ValidateOffhandWeapon(
		const proto::ItemEntry& entry,
		InventorySlot slot) const noexcept
	{
		ASSERT(slot.GetSlot() == player_equipment_slots::Offhand);

		const auto inventoryType = static_cast<inventory_type::Type>(entry.inventorytype());

		// Shields and holdables don't require dual wield
		if (inventoryType != inventory_type::Shield &&
			inventoryType != inventory_type::Holdable)
		{
			if (!m_context.CanDualWield())
			{
				return InventoryResult<void>::Failure(inventory_change_failure::CantDualWield);
			}
		}

		return InventoryResult<void>::Success();
	}
}
