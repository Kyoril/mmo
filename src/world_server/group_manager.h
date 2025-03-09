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
		explicit GroupManager() = default;

		~GroupManager() override = default;

	public:
		std::shared_ptr<PlayerGroup> AddGroup(uint64 id);

		void RemoveGroup(const uint64 id);

		void RemoveAllGroups();

		[[nodiscard]] std::shared_ptr<PlayerGroup> GetGroup(uint64 id) const;

	private:

		std::unordered_map<uint64, std::shared_ptr<PlayerGroup>> m_groups;
	};
}
