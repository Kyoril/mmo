// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <set>

#include "base/non_copyable.h"
#include "base/typedefs.h"

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
		[[nodiscard]] uint64 GetId() const noexcept
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
		[[nodiscard]] bool IsMember(const uint64 playerGuid) const noexcept { return m_members.contains(playerGuid); }

	private:
		uint64 m_guid;

		std::set<uint64> m_members;
	};
}
