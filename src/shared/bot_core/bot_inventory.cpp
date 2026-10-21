// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_inventory.h"

#include "bot_object_manager.h"

#include "game/item.h"

namespace mmo
{
	namespace
	{
		BotInventoryItem makeItem(const BotItemState& state, const uint8 bag, const uint8 slot, const bool equipped)
		{
			BotInventoryItem item;
			item.guid = state.guid;
			item.entry = state.entry;
			item.count = state.stackCount == 0 ? 1 : state.stackCount;
			item.bag = bag;
			item.slot = slot;
			item.equipped = equipped;
			item.durability = state.durability;
			item.maxDurability = state.maxDurability;
			return item;
		}
	}

	BotInventory BotInventory::Snapshot(const BotObjectManager& objects)
	{
		BotInventory inventory;
		constexpr uint8 mainBag = player_inventory_slots::Bag_0;

		for (uint8 slot = player_equipment_slots::Start; slot < player_equipment_slots::End; ++slot)
		{
			if (const BotItemState* state = objects.GetItem(objects.GetSelfInventorySlot(slot)))
			{
				inventory.m_items.push_back(makeItem(*state, mainBag, slot, true));
			}
		}

		for (uint8 slot = player_inventory_pack_slots::Start; slot < player_inventory_pack_slots::End; ++slot)
		{
			++inventory.m_totalSlots;
			if (const BotItemState* state = objects.GetItem(objects.GetSelfInventorySlot(slot)))
			{
				inventory.m_items.push_back(makeItem(*state, mainBag, slot, false));
			}
			else
			{
				++inventory.m_freeSlots;
			}
		}

		for (uint8 bagSlot = player_inventory_slots::Start; bagSlot < player_inventory_slots::End; ++bagSlot)
		{
			const BotItemState* bag = objects.GetItem(objects.GetSelfInventorySlot(bagSlot));
			if (!bag)
			{
				continue;
			}

			for (uint32 slot = 0; slot < bag->numSlots && slot < bag->slots.size(); ++slot)
			{
				++inventory.m_totalSlots;
				if (const BotItemState* state = objects.GetItem(bag->slots[slot]))
				{
					inventory.m_items.push_back(makeItem(*state, bagSlot, static_cast<uint8>(slot), false));
				}
				else
				{
					++inventory.m_freeSlots;
				}
			}
		}

		return inventory;
	}

	const BotInventoryItem* BotInventory::GetEquipped(const uint8 equipmentSlot) const
	{
		for (const BotInventoryItem& item : m_items)
		{
			if (item.equipped && item.slot == equipmentSlot)
			{
				return &item;
			}
		}

		return nullptr;
	}

	uint32 BotInventory::CountInBags(const uint32 entry) const
	{
		uint32 count = 0;
		for (const BotInventoryItem& item : m_items)
		{
			if (!item.equipped && item.entry == entry)
			{
				count += item.count;
			}
		}

		return count;
	}
}
