#pragma once

#include <memory>
#include <unordered_map>

#include "base/non_copyable.h"
#include "game_server/player_group.h"

namespace mmo
{
	class GroupManager final : public NonCopyable
	{
	public:
		/// Creates a new group manager.
		explicit GroupManager() = default;

		/// Default destructor.
		~GroupManager() override = default;

	public:
		/// Adds a new group with the given id.
		std::shared_ptr<PlayerGroup> AddGroup(uint64 id);

		/// Removes a group by id.
		void RemoveGroup(const uint64 id);

		/// Removes all groups.
		void RemoveAllGroups();

		/// Gets a group by id.
		[[nodiscard]] std::shared_ptr<PlayerGroup> GetGroup(uint64 id) const;

	private:

		std::unordered_map<uint64, std::shared_ptr<PlayerGroup>> m_groups;
	};
}
