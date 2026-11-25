// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/item.h"
#include "game/object_type_id.h"

namespace mmo
{
	/// Returned for slots without a field of the requested kind.
	constexpr uint32 InvalidField = 0xFFFFFFFF;

	/// @brief Gets the player field holding the item guid (two uint32) of a slot of bag 255.
	/// @param subslot Slot of bag 255: equipment, bag bar, pack, bank, bank bags, buyback or the glider slot.
	/// @return The field index, or InvalidField if the slot has no guid field.
	constexpr uint32 GetInventorySlotField(const uint8 subslot)
	{
		// Equipment, bag bar, pack, bank, bank bags and buyback are consecutive two-field blocks.
		if (subslot < player_buy_back_slots::End)
		{
			return object_fields::InvSlotHead + subslot * 2;
		}

		if (IsGliderSlot(subslot))
		{
			return object_fields::GliderSlot;
		}

		return InvalidField;
	}

	/// Number of fields of one visible item block.
	constexpr uint32 VisibleItemFieldStride = object_fields::VisibleItem2_CREATOR - object_fields::VisibleItem1_CREATOR;

	/// @brief Gets the player field holding the item entry id of a worn slot, which other clients
	///	read to draw the item.
	/// @return The field index, or InvalidField if the slot is not visible on the character.
	constexpr uint32 GetVisibleItemField(const uint8 subslot)
	{
		if (subslot < player_equipment_slots::End)
		{
			return object_fields::VisibleItem1_0 + subslot * VisibleItemFieldStride;
		}

		if (IsGliderSlot(subslot))
		{
			return object_fields::VisibleGlider_0;
		}

		return InvalidField;
	}

	/// @brief Gets the player field holding the creator guid of a worn slot's item.
	/// @return The field index, or InvalidField if the slot is not visible on the character.
	constexpr uint32 GetVisibleItemCreatorField(const uint8 subslot)
	{
		if (subslot < player_equipment_slots::End)
		{
			return object_fields::VisibleItem1_CREATOR + subslot * VisibleItemFieldStride;
		}

		if (IsGliderSlot(subslot))
		{
			return object_fields::VisibleGlider_CREATOR;
		}

		return InvalidField;
	}
}
