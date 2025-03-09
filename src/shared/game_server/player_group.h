
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
		explicit PlayerGroup(uint64 guid)
			: m_guid(guid)
		{
		}

		~PlayerGroup() override = default;

	public:
		[[nodiscard]] uint64 GetId() const noexcept
		{
			return m_guid;
		}

		void AddMember(uint64 playerGuid)
		{
			m_members.insert(playerGuid);
		}

		void RemoveMember(uint64 playerGuid)
		{
			m_members.erase(playerGuid);
		}

		bool IsMember(uint64 playerGuid) const noexcept { return m_members.contains(playerGuid); }

	private:
		uint64 m_guid;

		std::set<uint64> m_members;
	};
}
