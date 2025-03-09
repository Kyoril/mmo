
#include "group_manager.h"

namespace mmo
{
	std::shared_ptr<PlayerGroup> GroupManager::AddGroup(uint64 id)
	{
		if (m_groups.contains(id))
		{
			return nullptr;
		}

		auto group = std::make_shared<PlayerGroup>(id);
		m_groups[id] = group;
		return group;
	}

	void GroupManager::RemoveGroup(const uint64 id)
	{
		m_groups.erase(id);
	}

	void GroupManager::RemoveAllGroups()
	{
		m_groups.clear();
	}

	std::shared_ptr<PlayerGroup> GroupManager::GetGroup(const uint64 id) const
	{
		if (m_groups.contains(id))
		{
			return m_groups.at(id);
		}
		return nullptr;
	}
}
