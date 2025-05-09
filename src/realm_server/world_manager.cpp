// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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

	void WorldManager::AddWorld(const std::shared_ptr<World> added)
	{
		std::scoped_lock<std::mutex> lock{m_worldsMutex};

		ASSERT(added);
		m_worlds.push_back(added);
	}

	std::shared_ptr<World> WorldManager::GetIdealWorldNode(MapId mapId, InstanceId instanceId)
	{
		std::scoped_lock lock{ m_worldsMutex };

		if (!instanceId.is_nil())
		{
			const auto instanceIt = std::find_if(m_worlds.begin(), m_worlds.end(), [instanceId](const std::shared_ptr<World>& worldNode)
	            {
	                return worldNode->IsHostingInstanceId(instanceId);
	            }
			);

			if (instanceIt != m_worlds.end())
			{
				return *instanceIt;
			}
		}
		
		// TODO: Implement load balancer strategy here

		const auto mapIt = std::find_if(m_worlds.begin(), m_worlds.end(), [mapId](const std::shared_ptr<World>& worldNode)
            {
                return worldNode->IsHostingMapId(mapId);
            }
		);

		if (mapIt == m_worlds.end())
		{
			return nullptr;
		}

		return *mapIt;
	}

	std::shared_ptr<World> WorldManager::GetWorldByInstanceId(InstanceId instanceId)
	{
		const auto w = std::find_if(
			m_worlds.begin(),
			m_worlds.end(),
			[instanceId](const std::shared_ptr<World>& w)
			{
				return w->IsHostingInstanceId(instanceId);
			});

		if (w != m_worlds.end())
		{
			return *w;
		}

		return nullptr;
	}
}
