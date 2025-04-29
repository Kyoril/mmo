// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/signal.h"

#include "shared/proto_data/loot_entry.pb.h"

#include <map>

#include "condition_mgr.h"
#include "proto_data/project.h"

namespace io
{
	class Writer;
}

namespace mmo
{
	class GamePlayerS;

	/// This struct defines an item entry in the LootInstance class.
	struct LootItem final
	{
		/// Indicates whether the item has been looted by a player.
		bool isLooted;

		/// Gets the remaining number of this item in the loot instance to support partial looting item stacks.
		uint32 count;

		/// The loot definition of this item which contains what item it is and other loot-related properties.
		const proto::LootDefinition& definition;

		/// Default constructor.
		/// @param count The number of items in this loot item.
		/// @param def The loot definition of this item.
		explicit LootItem(const uint32 count, const proto::LootDefinition& def)
			: isLooted(false)
			, count(count)
			, definition(def)
		{
		}
	};

	/// This class represents some loot container instance. It stores items and gold that can be looted by several players.
	/// Which players can loot is also stored in this class.
	class LootInstance
	{
		friend io::Writer& operator << (io::Writer& w, LootInstance const& loot);

	public:
		/// This signal is triggered when the loot instance is cleared (all items and gold removed).
		signal<void()> cleared;

		/// This signal is triggered when a player closed the loot dialog while looting this instance. Note that for party
		/// loot this does not necessarily mean that no other player has this loot instance open at the same time.
		/// @param playerId The GUID of the player who closed the loot dialog.
		signal<void(uint64 playerId)> closed;

		/// This signal is triggered when all gold was removed from this loot. There might still be items remaining.
		signal<void()> goldRemoved;

		/// This signal is triggered when an item was removed from this loot instance.
		/// @param slot The slot of the item that was removed.
		signal<void(uint8 slot)> itemRemoved;

	public:
		typedef std::map<uint32, uint32> PlayerItemLootEntry;
		typedef std::map<uint64, PlayerItemLootEntry> PlayerLootEntries;

	public:
		/// Initializes a new instance of the loot instance.
		explicit LootInstance(const proto::ItemManager& items, const ConditionMgr& conditionMgr, uint64 lootGuid);

		explicit LootInstance(const proto::ItemManager& items, const ConditionMgr& conditionMgr, uint64 lootGuid, const proto::LootEntry* entry, uint32 minGold, uint32 maxGold, const std::vector<std::weak_ptr<GamePlayerS>>& lootRecipients);

	public:
		/// Returns the id of this loot instance.
		[[nodiscard]] uint64 GetLootGuid() const { return m_lootGuid; }

		/// Determines whether the loot is empty.
		[[nodiscard]] bool IsEmpty() const;

		/// Determines whether a certain character can receive any loot from this instance.
		/// @param receiver The receivers GUID.
		[[nodiscard]] bool ContainsLootFor(uint64 receiver);

		/// Gets the amount of remaining gold in this loot instance.
		/// @returns The amount of gold in this loot instance.
		[[nodiscard]] uint32 GetGold() const { return m_gold; }

		/// Removes all gold from the loot instance.
		void TakeGold();

		/// Determines if there is gold available to loot.
		/// @returns true if there is gold available, false otherwise.
		[[nodiscard]] bool HasGold() const { return m_gold != 0; }

		/// Get loot item definition from the requested slot.
		/// @param slot The slot of the item to get.
		/// @returns nullptr if the slot is invalid, otherwise the loot item pointer at the given slot.
		[[nodiscard]] const LootItem* GetLootDefinition(uint8 slot) const;

		/// Tries to consume all items at a given slot for a specific player.
		bool TakeItem(uint8 slot, uint64 receiver);

		/// Gets the number of items.
		[[nodiscard]] uint32 GetItemCount() const { return static_cast<uint32>(m_items.size()); }

		void Serialize(io::Writer& writer, uint64 receiver) const;

	private:

		void AddLootItem(const proto::LootDefinition& def);

	private:

		const proto::ItemManager& m_itemManager;
		const ConditionMgr& m_conditionMgr;
		uint64 m_lootGuid;
		uint32 m_gold;
		std::vector<LootItem> m_items;
		std::vector<uint64> m_recipients;
		PlayerLootEntries m_playerLootData;
	};
}
