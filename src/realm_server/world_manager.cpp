// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "world_manager.h"
#include "world.h"

#include "base/macros.h"

#include <cassert>


namespace mmo
{
	WorldManager::WorldManager(
	    size_t capacity)
		: m_capacity(capacity)
	{
	}

	WorldManager::~WorldManager() = default;

	void WorldManager::WorldDisconnected(World & world)
	{
		std::scoped_lock<std::mutex> lock{ m_worldsMutex};

		const auto p = std::find_if(
			m_worlds.begin(),
			m_worlds.end(),
			[&world](const std::shared_ptr<World> &p)
		{
			return (&world == p.get());
		});
		ASSERT(p != m_worlds.end());
		m_worlds.erase(p);
	}
	
	bool WorldManager::HasCapacityBeenReached()
	{
		std::scoped_lock<std::mutex> lock{ m_worldsMutex };
		return m_worlds.size() >= m_capacity;
	}

	void WorldManager::AddWorld(std::shared_ptr<World> added)
	{
		std::scoped_lock<std::mutex> lock{m_worldsMutex};

		ASSERT(added);
		m_worlds.push_back(added);
	}

	std::weak_ptr<World> WorldManager::GetWorldByMapId(uint64 mapId)
	{
		std::scoped_lock<std::mutex> lock{ m_worldsMutex };

		const auto p = std::find_if(
			m_worlds.begin(),
			m_worlds.end(),
			[mapId](const std::shared_ptr<World>& p)
			{
				return p->IsHostingMapId(mapId);
			}
		);

		if (p == m_worlds.end())
		{
			return std::weak_ptr<World>();
		}

		return *p;
	}
}
