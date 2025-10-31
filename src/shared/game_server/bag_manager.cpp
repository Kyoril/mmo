// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bag_manager.h"
#include "inventory_types.h"
#include "game_server/objects/game_bag_s.h"
#include "game_server/objects/game_item_s.h"
#include "game/item.h"
#include "base/macros.h"

#include <memory>

namespace mmo
{
	BagManager::BagManager(IBagManagerContext& context) noexcept
		: m_context(context)
	{
	}

	std::shared_ptr<GameBagS> BagManager::GetBag(InventorySlot slot) const noexcept
	{
		uint16 absoluteSlot = slot.GetAbsolute();

		// Ensure we're working with bag pack slot format
		if (!IsBagPackSlot(absoluteSlot))
		{
			// Convert bag slot (0xBBSS format) to bag pack slot (0xFFBB format)
			const uint8 bagId = static_cast<uint8>(absoluteSlot >> 8);
			absoluteSlot = (player_inventory_slots::Bag_0 << 8) | bagId;
		}

		// Get item at slot and verify it's a container
		auto item = m_context.GetItemAtSlot(absoluteSlot);
		if (!item || item->GetTypeId() != ObjectTypeId::Container)
		{
			return nullptr;
		}

		return std::dynamic_pointer_cast<GameBagS>(item);
	}

	void BagManager::UpdateBagSlot(
		std::shared_ptr<GameItemS> item,
		uint8 bagSlot,
		uint8 itemSlot) const noexcept
	{
		ASSERT(item);

		// Get the bag at the specified bag slot
		const uint16 bagPackSlot = (player_inventory_slots::Bag_0 << 8) | bagSlot;
		auto bag = m_context.GetItemAtSlot(bagPackSlot);

		if (!bag || bag->GetTypeId() != ObjectTypeId::Container)
		{
			return;
		}

		auto bagInst = std::dynamic_pointer_cast<GameBagS>(bag);
		ASSERT(bagInst);

		// Update the bag's slot field to reference the item's GUID
		bagInst->Set<uint64>(object_fields::Slot_1 + (itemSlot * 2), item->GetGuid());

		// Notify that the bag was updated
		m_context.NotifyItemUpdated(bagInst, bagPackSlot);
	}

	int32 BagManager::CalculateEquipBagSlotChange(const GameBagS& bag) const noexcept
	{
		// Equipping a bag adds its slot count to available slots
		return static_cast<int32>(bag.GetSlotCount());
	}

	int32 BagManager::CalculateUnequipBagSlotChange(const GameBagS& bag) const noexcept
	{
		// Unequipping a bag removes its slot count from available slots
		return -static_cast<int32>(bag.GetSlotCount());
	}

	int32 BagManager::CalculateSwapBagSlotChange(
		const GameBagS* oldBag,
		const GameBagS* newBag) const noexcept
	{
		int32 change = 0;

		// Remove old bag's slots if it exists
		if (oldBag)
		{
			change -= static_cast<int32>(oldBag->GetSlotCount());
		}

		// Add new bag's slots if it exists
		if (newBag)
		{
			change += static_cast<int32>(newBag->GetSlotCount());
		}

		return change;
	}

	uint16 BagManager::ConvertToBagPackSlot(uint16 slot) const noexcept
	{
		if (IsBagPackSlot(slot))
		{
			return slot;
		}

		// Extract bag ID from 0xBBSS format and convert to 0xFFBB
		const uint8 bagId = static_cast<uint8>(slot >> 8);
		return (player_inventory_slots::Bag_0 << 8) | bagId;
	}

	bool BagManager::IsBagPackSlot(uint16 slot) noexcept
	{
		// Bag pack slots use format 0xFFXX where 0xFF is Bag_0 (255)
		const uint8 bag = static_cast<uint8>(slot >> 8);
		return bag == player_inventory_slots::Bag_0;
	}
}