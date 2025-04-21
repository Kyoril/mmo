// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/signal.h"

#include "shared/proto_data/loot_entry.pb.h"

#include <map>

#include "proto_data/project.h"

namespace io
{
	class Writer;
}

namespace mmo
{
	class GamePlayerS;

	struct LootItem final
	{
		bool isLooted;
		uint32 count;
		const proto::LootDefinition& definition;

		explicit LootItem(uint32 count, const proto::LootDefinition& def)
			: isLooted(false)
			, count(count)
			, definition(def)
		{
		}
	};

	class LootInstance
	{
		friend io::Writer& operator << (io::Writer& w, LootInstance const& loot);

	public:
		signal<void()> cleared;

		signal<void(uint64)> closed;

		signal<void()> goldRemoved;

		signal<void(uint8)> itemRemoved;

	public:

		typedef std::map<uint32, uint32> PlayerItemLootEntry;
		typedef std::map<uint64, PlayerItemLootEntry> PlayerLootEntries;


	public:

		/// Initializes a new instance of the loot instance.
		explicit LootInstance(const proto::ItemManager& items, uint64 lootGuid);

		explicit LootInstance(const proto::ItemManager& items, uint64 lootGuid, const proto::LootEntry* entry, uint32 minGold, uint32 maxGold, const std::vector<std::weak_ptr<GamePlayerS>>& lootRecipients);

		///
		uint64 getLootGuid() const { return m_lootGuid; }

		/// Determines whether the loot is empty.
		bool IsEmpty() const;

		/// Determines whether a certain character can receive any loot from this instance.
		/// @param receiver The receivers GUID.
		bool ContainsLootFor(uint64 receiver);

		///
		uint32 getGold() const { return m_gold; }

		///
		void TakeGold();

		/// Determines if there is gold available to loot.
		bool HasGold() const { return m_gold != 0; }

		/// Get loot item definition from the requested slot.
		const LootItem* GetLootDefinition(uint8 slot) const;

		///
		void TakeItem(uint8 slot, uint64 receiver);

		/// Gets the number of items.
		uint32 getItemCount() const { return m_items.size(); }

		void Serialize(io::Writer& writer, uint64 receiver) const;

	private:

		void AddLootItem(const proto::LootDefinition& def);

	private:

		const proto::ItemManager& m_itemManager;
		uint64 m_lootGuid;
		uint32 m_gold;
		std::vector<LootItem> m_items;
		std::vector<uint64> m_recipients;
		PlayerLootEntries m_playerLootData;
	};
}
