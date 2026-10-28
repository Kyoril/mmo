// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <vector>

namespace mmo
{
	class BotObjectManager;

	/// One item the bot carries, with the address the server uses for it.
	struct BotInventoryItem final
	{
		uint64 guid { 0 };
		uint32 entry { 0 };
		uint32 count { 0 };

		/// Bag and slot as the inventory packets take them: bag 255 with the absolute slot for
		/// equipment and backpack, otherwise the bag's inventory slot (19-22) and the slot in it.
		uint8 bag { 255 };
		uint8 slot { 0 };

		bool equipped { false };

		uint32 durability { 0 };
		uint32 maxDurability { 0 };

		/// The absolute slot, (bag << 8) | slot, as trade takes it.
		[[nodiscard]] uint16 GetAbsoluteSlot() const { return static_cast<uint16>((bag << 8) | slot); }
	};

	/// What the bot carries, read off its own player and bag objects at one moment.
	///
	/// A snapshot rather than a live view: the AI decides on one consistent picture per tick, and
	/// the object manager underneath changes with every update packet.
	class BotInventory final
	{
	public:
		/// Builds a snapshot from the bot's own inventory slots and the items it knows.
		[[nodiscard]] static BotInventory Snapshot(const BotObjectManager& objects);

		/// Every equipped item and every item in the backpack and bags.
		[[nodiscard]] const std::vector<BotInventoryItem>& GetItems() const { return m_items; }

		/// Free slots across the backpack and all equipped bags.
		[[nodiscard]] uint32 GetFreeSlots() const { return m_freeSlots; }

		/// Slots across the backpack and all equipped bags, used or not.
		[[nodiscard]] uint32 GetTotalSlots() const { return m_totalSlots; }

		/// The item equipped in an equipment slot (player_equipment_slots), or null.
		[[nodiscard]] const BotInventoryItem* GetEquipped(uint8 equipmentSlot) const;

		/// Total count of an item entry in the backpack and bags; equipped items do not count.
		[[nodiscard]] uint32 CountInBags(uint32 entry) const;

	private:
		std::vector<BotInventoryItem> m_items;
		uint32 m_freeSlots { 0 };
		uint32 m_totalSlots { 0 };
	};
}
