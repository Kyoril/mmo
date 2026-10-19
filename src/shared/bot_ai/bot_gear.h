// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <vector>

namespace mmo
{
	namespace proto
	{
		class ItemEntry;
	}

	/// The equipment slots (player_equipment_slots) an item of the given inventory type can go
	/// into, best slot first. Empty for items that are not worn, including bags.
	[[nodiscard]] std::vector<uint8> EquipmentSlotsFor(uint32 inventoryType);

	/// How much a class wants an item, for comparing two items that fit the same slot.
	///
	/// Deliberately coarse: the class's primary stat counts most, stamina and armor next, weapon
	/// damage per second for classes that fight in melee, and item level breaks ties. It only has
	/// to tell an upgrade from a downgrade, not tune a build.
	///
	/// @param classId Class id as in the class data (0 mage, 1 warrior, 2 cleric, 3 acolyte, 4 scout).
	[[nodiscard]] float ScoreItem(const proto::ItemEntry& item, uint32 classId);

	/// Whether an item is worth nothing but its sell price: grey quality.
	[[nodiscard]] bool IsJunk(const proto::ItemEntry& item);

	/// Whether an item is worn (armor, weapon, jewelry), as opposed to bags, consumables,
	/// reagents, trade goods and quest items.
	[[nodiscard]] bool IsWearable(const proto::ItemEntry& item);
}
