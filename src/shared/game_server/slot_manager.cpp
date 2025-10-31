// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "slot_manager.h"
#include "game_server/objects/game_item_s.h"
#include "game_server/objects/game_bag_s.h"
#include "shared/proto_data/items.pb.h"

namespace mmo
{
	SlotManager::SlotManager(const ISlotManagerContext& context) noexcept
		: m_context(context)
	{
	}

	uint16 SlotManager::FindFirstEmptySlot() const noexcept
	{
		uint16 targetSlot = 0;
		
		ForEachBag([this, &targetSlot](uint8 bag, uint8 slotStart, uint8 slotEnd) -> bool
		{
			for (uint8 slot = slotStart; slot < slotEnd; ++slot)
			{
				const uint16 absoluteSlot = InventorySlot::FromRelative(bag, slot).GetAbsolute();

				// Check if this slot is empty
				if (IsSlotEmpty(absoluteSlot))
				{
					// Slot is empty - use it!
					targetSlot = absoluteSlot;
					return false;  // Stop iteration
				}
			}

			// Continue with the next bag
			return true;
		});

		return targetSlot;
	}

	InventoryResult<void> SlotManager::FindAvailableSlots(
		const proto::ItemEntry& entry,
		uint16 amount,
		SlotAllocationResult& result) const noexcept
	{
		const uint16 itemCount = m_context.GetItemCount(entry.id());
		const uint16 requiredSlots = (amount - 1) / entry.maxstack() + 1;
		uint16 itemsProcessed = 0;

		ForEachBag([&](uint8 bag, uint8 slotStart, uint8 slotEnd) -> bool
		{
			for (uint8 slot = slotStart; slot < slotEnd; ++slot)
			{
				const uint16 absoluteSlot = InventorySlot::FromRelative(bag, slot).GetAbsolute();
				auto item = m_context.GetItemAtSlot(absoluteSlot);

				if (!item)
				{
					// Empty slot found
					result.availableStacks += entry.maxstack();
					result.emptySlots.add(absoluteSlot);

					if (itemsProcessed >= itemCount && result.emptySlots.size() >= requiredSlots)
					{
						break;
					}
					continue;
				}

				// Check if item matches entry
				if (item->GetEntry().id() != entry.id())
				{
					continue;
				}

				const uint32 stackCount = item->GetStackCount();
				itemsProcessed += stackCount;

				// Check if stack can accept more items
				if (stackCount < entry.maxstack())
				{
					result.availableStacks += (entry.maxstack() - stackCount);
					result.usedCapableSlots.add(absoluteSlot);
				}

				if (itemsProcessed >= itemCount && result.emptySlots.size() >= requiredSlots)
				{
					break;
				}
			}

			return itemsProcessed < itemCount || result.emptySlots.size() < requiredSlots;
		});

		if (amount > result.availableStacks)
		{
			return InventoryResult<void>::Failure(inventory_change_failure::InventoryFull);
		}

		return InventoryResult<void>::Success();
	}

	void SlotManager::ForEachBag(const BagCallback& callback) const noexcept
	{
		// Enumerates all possible bags
		static constexpr std::array<uint8, 5> bags =
		{
			player_inventory_slots::Bag_0,
			player_inventory_slots::Start + 0,
			player_inventory_slots::Start + 1,
			player_inventory_slots::Start + 2,
			player_inventory_slots::Start + 3
		};

		for (const auto& bag : bags)
		{
			uint8 slotStart = 0, slotEnd = 0;
			if (!GetBagSlotRange(bag, slotStart, slotEnd))
			{
				continue;  // Skip this bag
			}

			if (!callback(bag, slotStart, slotEnd))
			{
				break;  // Stop iteration requested by callback
			}
		}
	}

	uint16 SlotManager::CountFreeSlots() const noexcept
	{
		uint16 count = 0;
		
		ForEachBag([this, &count](uint8 bag, uint8 slotStart, uint8 slotEnd) -> bool
		{
			for (uint8 slot = slotStart; slot < slotEnd; ++slot)
			{
				const uint16 absoluteSlot = InventorySlot::FromRelative(bag, slot).GetAbsolute();
				if (IsSlotEmpty(absoluteSlot))
				{
					++count;
				}
			}
			
			return true;  // Continue iteration
		});

		return count;
	}

	bool SlotManager::IsSlotEmpty(uint16 slot) const noexcept
	{
		return m_context.GetItemAtSlot(slot) == nullptr;
	}

	bool SlotManager::GetBagSlotRange(uint8 bagId, uint8& outStartSlot, uint8& outEndSlot) const noexcept
	{
		if (bagId == player_inventory_slots::Bag_0)
		{
			// Main inventory bag
			outStartSlot = player_inventory_pack_slots::Start;
			outEndSlot = player_inventory_pack_slots::End;
			return outEndSlot > outStartSlot;
		}
		
		// Check for equipped bag
		const uint16 absoluteBagSlot = InventorySlot::FromRelative(player_inventory_slots::Bag_0, bagId).GetAbsolute();
		auto bag = m_context.GetBagAtSlot(absoluteBagSlot);
		
		if (!bag)
		{
			return false;  // No bag equipped in this slot
		}

		outStartSlot = 0;
		outEndSlot = bag->GetSlotCount();
		
		return outEndSlot > outStartSlot;
	}
}
