// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "inventory_types.h"

namespace mmo
{
	InventorySlot InventorySlot::FromAbsolute(uint16 absolute)
	{
		return InventorySlot(absolute);
	}

	InventorySlot InventorySlot::FromRelative(uint8 bag, uint8 slot)
	{
		const uint16 absolute = (static_cast<uint16>(bag) << 8) | static_cast<uint16>(slot);
		return InventorySlot(absolute);
	}

	bool InventorySlot::IsEquipment() const noexcept
	{
		return (GetBag() == player_inventory_slots::Bag_0) && 
		       (GetSlot() < player_equipment_slots::End);
	}

	bool InventorySlot::IsBag() const noexcept
	{
		return (GetBag() >= player_inventory_slots::Start) && 
		       (GetBag() < player_inventory_slots::End);
	}

	bool InventorySlot::IsBagPack() const noexcept
	{
		return (GetBag() == player_inventory_slots::Bag_0) && 
		       (GetSlot() >= player_inventory_slots::Start) && 
		       (GetSlot() < player_inventory_slots::End);
	}

	bool InventorySlot::IsInventory() const noexcept
	{
		return (GetBag() == player_inventory_slots::Bag_0) && 
		       (GetSlot() >= player_inventory_pack_slots::Start) && 
		       (GetSlot() < player_inventory_pack_slots::End);
	}

	bool InventorySlot::IsBagBar() const noexcept
	{
		return (GetBag() == player_inventory_slots::Bag_0) && 
		       (GetSlot() >= player_inventory_slots::Start) && 
		       (GetSlot() < player_inventory_slots::End);
	}

	bool InventorySlot::IsBuyBack() const noexcept
	{
		return (GetSlot() >= player_buy_back_slots::Start) && 
		       (GetSlot() < player_buy_back_slots::End);
	}
}
