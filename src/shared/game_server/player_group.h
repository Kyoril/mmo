// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <set>
#include <vector>

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "game/group.h"
#include "game/item.h"

namespace mmo
{
	/// This class represents a player group on a world node. It is synchronized between a realm (who owns the group) and world nodes.
	class PlayerGroup final : public NonCopyable
	{
	public:
		/// Creates a new instance of the PlayerGroup class and initializes it with a group GUID.
		/// @param groupId The id of this group.
		explicit PlayerGroup(const uint64 groupId)
			: m_guid(groupId)
		{
		}

		/// Default destructor.
		~PlayerGroup() override = default;

	public:
		/// Returns the group GUID.
		/// @returns The unique group identifier as uint64.
		[[nodiscard]] uint64 GetId() const
		{
			return m_guid;
		}

		/// Adds a player to the group (referenced by player id). Does nothing if the player is already a group member.
		/// @param playerGuid The player GUID.
		void AddMember(const uint64 playerGuid)
		{
			m_members.insert(playerGuid);
		}

		/// Removes a player from the group (referenced by player id). Does nothing if the player is not a group member.
		/// @param playerGuid The player GUID.
		void RemoveMember(const uint64 playerGuid)
		{
			m_members.erase(playerGuid);
		}

		/// Determines whether a player is a member of the group.
		/// @param playerGuid The player GUID to check.
		/// @returns true if the player is a member, false otherwise.
		[[nodiscard]] bool IsMember(const uint64 playerGuid) const { return m_members.contains(playerGuid); }

		/// Gets the current loot method for this group.
		[[nodiscard]] LootMethod GetLootMethod() const noexcept { return m_lootMethod; }

		/// Gets the current loot quality threshold for this group.
		[[nodiscard]] uint8 GetLootThreshold() const noexcept { return m_lootThreshold; }

		/// Sets the loot method and quality threshold for this group.
		/// @param method The loot method to use.
		/// @param threshold The item quality threshold used for group loot rolls.
		void SetLootMethod(const LootMethod method, const uint8 threshold = item_quality::Uncommon) noexcept
		{
			m_lootMethod = method;
			m_lootThreshold = threshold;
		}

		/// Gets the loot master's GUID (only meaningful for MasterLoot).
		[[nodiscard]] uint64 GetLootMasterGuid() const noexcept { return m_lootMasterGuid; }

		/// Sets the loot master GUID.
		void SetLootMasterGuid(const uint64 guid) noexcept { m_lootMasterGuid = guid; }

		/// Chooses the next round-robin looter from the nearby members and advances the cursor.
		/// @param nearbyMembers The nearby members eligible for round-robin looting.
		/// @returns The chosen looter GUID, or 0 if no nearby member was provided.
		uint64 GetNextRoundRobinRecipient(const std::vector<uint64>& nearbyMembers)
		{
			if (nearbyMembers.empty())
			{
				return 0;
			}

			for (size_t index = 0; index < nearbyMembers.size(); ++index)
			{
				const size_t candidateIndex = (m_roundRobinIndex + index) % nearbyMembers.size();
				const uint64 candidateGuid = nearbyMembers[candidateIndex];
				if (m_members.contains(candidateGuid))
				{
					m_roundRobinIndex = (candidateIndex + 1) % nearbyMembers.size();
					return candidateGuid;
				}
			}

			m_roundRobinIndex = 1 % nearbyMembers.size();
			return nearbyMembers[0];
		}

	private:
		uint64 m_guid;
		std::set<uint64> m_members;
		LootMethod m_lootMethod = loot_method::FreeForAll;
		uint8 m_lootThreshold = item_quality::Uncommon;
		uint64 m_lootMasterGuid = 0;
		size_t m_roundRobinIndex = 0;
	};
}
