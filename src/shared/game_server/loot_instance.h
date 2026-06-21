// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/signal.h"

#include "shared/proto_data/loot_entry.pb.h"

#include <map>
#include <set>

#include "condition_mgr.h"
#include "proto_data/project.h"
#include "game/group.h"
#include "game/item.h"

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

		/// The GUID of the player assigned to receive this item (used for RoundRobin loot method). 0 = unassigned.
		uint64 assignedRecipientGuid = 0;

		/// Indicates whether this item requires a group roll before it can be looted.
		bool needsGroupRoll = false;

		/// Indicates whether the roll for this item has completed.
		bool rollComplete = false;

		/// Stores the roll winner's GUID once a group roll has completed.
		uint64 rollWinner = 0;

		/// Default constructor.
		/// @param count The number of items in this loot item.
		/// @param def The loot definition of this item.
		explicit LootItem(const uint32 count, const proto::LootDefinition& def)
			: isLooted(false)
			, count(count)
			, definition(def)
			, assignedRecipientGuid(0)
		{
		}
	};

	/// Tracks roll votes and the eligible players for a single loot slot.
	struct LootRollData final
	{
		std::map<uint64, RollVote> votes;
		std::map<uint64, uint8> rollValues;
		std::map<uint64, String> playerNames;
		std::set<uint64> eligiblePlayers;
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

		/// This signal is triggered when a loot roll was resolved.
		/// @param lootGuid The loot source GUID.
		/// @param slot The loot slot that was rolled on.
		/// @param itemId The item entry id.
		/// @param winnerGuid The winner GUID, or 0 if everyone passed.
		/// @param winningRoll The winning roll value.
		/// @param winningVote The winning vote type.
		/// @param winnerName The name of the winner.
		signal<void(uint64 lootGuid, uint8 slot, uint32 itemId, uint64 winnerGuid, uint8 winningRoll, RollVote winningVote, const String& winnerName)> rollWon;

		/// This signal is triggered when a player submits their vote for a loot roll.
		/// @param lootGuid The loot source GUID.
		/// @param slot The loot slot.
		/// @param itemId The item entry id.
		/// @param playerGuid The player who voted.
		/// @param vote The vote type.
		/// @param rollValue The roll value (1-100 for Need/Greed, 0 for Pass).
		/// @param playerName The name of the voting player.
		signal<void(uint64 lootGuid, uint8 slot, uint32 itemId, uint64 playerGuid, RollVote vote, uint8 rollValue, const String& playerName)> rollVoted;

	public:
		typedef std::map<uint32, uint32> PlayerItemLootEntry;
		typedef std::map<uint64, PlayerItemLootEntry> PlayerLootEntries;

	public:
		/// Initializes a new instance of the loot instance.
		explicit LootInstance(const proto::ItemManager& items, const ConditionMgr& conditionMgr, uint64 lootGuid);

		/// Initializes a new instance of the loot instance with a loot entry and recipients.
		/// @param items The item manager used to look up item templates.
		/// @param conditionMgr The condition manager used to evaluate quest item conditions.
		/// @param lootGuid The unique GUID of the loot source object.
		/// @param entry The loot entry template defining possible drops.
		/// @param minGold Minimum gold to generate.
		/// @param maxGold Maximum gold to generate.
		/// @param lootRecipients The list of eligible loot recipients.
		/// @param lootMethod The loot distribution method to enforce. Defaults to FreeForAll.
		/// @param lootMasterGuid The GUID of the loot master (only meaningful for MasterLoot). Defaults to 0.
		explicit LootInstance(const proto::ItemManager& items, const ConditionMgr& conditionMgr, uint64 lootGuid,
			const proto::LootEntry* entry, uint32 minGold, uint32 maxGold,
			const std::vector<std::weak_ptr<GamePlayerS>>& lootRecipients,
			LootMethod lootMethod = loot_method::FreeForAll,
			uint64 lootMasterGuid = 0);

		/// Initializes a new instance of the loot instance from multiple loot entries (modular loot tables).
		/// Each entry is rolled independently and the generated items are combined. The gold of every entry
		/// is rolled and summed up.
		/// @param items The item manager used to look up item templates.
		/// @param conditionMgr The condition manager used to evaluate quest item conditions.
		/// @param lootGuid The unique GUID of the loot source object.
		/// @param entries The loot entry templates defining possible drops. Null entries are ignored.
		/// @param lootRecipients The list of eligible loot recipients.
		/// @param lootMethod The loot distribution method to enforce. Defaults to FreeForAll.
		/// @param lootMasterGuid The GUID of the loot master (only meaningful for MasterLoot). Defaults to 0.
		explicit LootInstance(const proto::ItemManager& items, const ConditionMgr& conditionMgr, uint64 lootGuid,
			const std::vector<const proto::LootEntry*>& entries,
			const std::vector<std::weak_ptr<GamePlayerS>>& lootRecipients,
			LootMethod lootMethod = loot_method::FreeForAll,
			uint64 lootMasterGuid = 0);

	public:
		/// Returns the id of this loot instance.
		[[nodiscard]] uint64 GetLootGuid() const { return m_lootGuid; }

		/// Determines whether the loot is empty.
		[[nodiscard]] bool IsEmpty() const;

		/// Determines whether a certain character can receive any loot from this instance.
		/// @param receiver The receivers GUID.
		[[nodiscard]] bool ContainsLootFor(uint64 receiver);

		/// Determines whether a specific receiver may loot a given slot right now.
		/// @param slot The server-side loot slot index.
		/// @param receiver The GUID of the player attempting to loot the slot.
		/// @returns true if the slot may be looted by the receiver, false otherwise.
		[[nodiscard]] bool CanLootItem(uint8 slot, uint64 receiver) const;

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

		/// Sets the designated looter for round-robin and low-quality group loot items.
		/// @param guid The designated looter GUID, or 0 if none is designated.
		void SetRoundRobinLooter(uint64 guid) noexcept { m_roundRobinLooter = guid; }

		/// Gets the designated looter for round-robin and low-quality group loot items.
		[[nodiscard]] uint64 GetRoundRobinLooter() const noexcept { return m_roundRobinLooter; }

		/// Updates the loot method and loot quality threshold used by this loot instance.
		/// @param lootMethod The loot method to enforce.
		/// @param lootThreshold The quality threshold used for group loot rolls.
		void SetLootMethod(LootMethod lootMethod, uint8 lootThreshold) noexcept
		{
			m_lootMethod = lootMethod;
			m_lootThreshold = lootThreshold;
		}

		/// Gets the loot quality threshold used by this loot instance.
		[[nodiscard]] uint8 GetLootThreshold() const noexcept { return m_lootThreshold; }

		/// Marks items at or above the quality threshold as requiring a group roll.
		/// @param nearbyPlayers The nearby eligible players who may vote on the roll.
		void SetupGroupRollItems(const std::set<uint64>& nearbyPlayers);

		/// Submits a loot-roll vote for a slot.
		/// @param slot The slot being rolled on.
		/// @param playerGuid The player casting the vote.
		/// @param vote The vote type.
		/// @param playerName The name of the voting player.
		/// @returns true if the vote was accepted, false otherwise.
		bool SubmitRollVote(uint8 slot, uint64 playerGuid, RollVote vote, const String& playerName);

		/// Gets the loot recipients for this instance.
		[[nodiscard]] const std::vector<uint64>& GetRecipients() const noexcept { return m_recipients; }

		/// Gets the generated loot items.
		[[nodiscard]] const std::vector<LootItem>& GetItems() const noexcept { return m_items; }

		/// Gets the active roll state map keyed by loot slot.
		[[nodiscard]] const std::map<uint8, LootRollData>& GetRollDataMap() const noexcept { return m_rollData; }

		/// Determines whether this loot instance still has any active group rolls.
		[[nodiscard]] bool HasActiveRolls() const noexcept { return !m_rollData.empty(); }

		/// Determines whether the roll UI/start packets were already emitted for this loot instance.
		[[nodiscard]] bool AreRollsStarted() const noexcept { return m_rollsStarted; }

		/// Marks the active rolls as started so packets are only emitted once.
		void MarkRollsStarted() noexcept { m_rollsStarted = true; }

		void Serialize(io::Writer& writer, uint64 receiver) const;

	private:

		void AddLootItem(const proto::LootDefinition& def);

		/// Rolls a single loot entry's groups and adds the resulting items to this instance.
		/// @param entry The loot entry to roll.
		/// @param lootRecipients The eligible loot recipients (used to evaluate item conditions).
		void GenerateItemsFromEntry(const proto::LootEntry& entry, const std::vector<std::weak_ptr<GamePlayerS>>& lootRecipients);

		[[nodiscard]] uint8 GetSlotType(uint8 slot, uint64 receiver) const;

		void ResolveRoll(uint8 slot);

	private:

		const proto::ItemManager& m_itemManager;
		const ConditionMgr& m_conditionMgr;
		uint64 m_lootGuid;
		uint32 m_gold;
		std::vector<LootItem> m_items;
		std::vector<uint64> m_recipients;
		PlayerLootEntries m_playerLootData;
		uint64 m_roundRobinLooter = 0;
		LootMethod m_lootMethod = loot_method::FreeForAll;
		uint8 m_lootThreshold = item_quality::Uncommon;
		uint64 m_lootMasterGuid = 0;
		std::map<uint8, LootRollData> m_rollData;
		bool m_rollsStarted = false;
	};
}
