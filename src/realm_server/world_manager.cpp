// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_manager.h"
#include "world.h"
#include "player.h"
#include "player_manager.h"
#include "log/default_log_levels.h"

#include <vector>

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

	void WorldManager::DisconnectAll()
	{
		// Copy out under the lock: Disconnect() removes the world from this manager, which takes
		// the same mutex.
		std::vector<std::shared_ptr<World>> worlds;
		{
			std::scoped_lock scopedLock{ m_worldsMutex };
			worlds.assign(m_worlds.begin(), m_worlds.end());
		}

		for (const auto& world : worlds)
		{
			world->Disconnect();
		}
	}

	void WorldManager::BroadcastTimeOfDay(const GameTime timeOfDay, const uint32 transitionMs)
	{
		std::scoped_lock scopedLock{ m_worldsMutex };
		for (const auto& world : m_worlds)
		{
			if (world->IsAuthenticated())
			{
				world->SendTimeOfDay(timeOfDay, transitionMs);
			}
		}
	}

	void WorldManager::BroadcastShutdown()
	{
		std::scoped_lock scopedLock{ m_worldsMutex };
		for (const auto& world : m_worlds)
		{
			if (world->IsAuthenticated())
			{
				world->SendShutdown();
			}
		}
	}

	size_t WorldManager::GetWorldCount()
	{
		std::scoped_lock scopedLock{ m_worldsMutex };
		return m_worlds.size();
	}

	bool WorldManager::SetSubsystemEnabled(const game::Subsystem subsystem, const bool enabled, PlayerManager& playerManager)
	{
		if (subsystem >= game::subsystem::Count_)
		{
			return false;
		}

		ILOG("Subsystem " << game::GetSubsystemName(subsystem) << (enabled ? " enabled" : " disabled") << " realm-wide");

		if (game::GetSubsystemOwner(subsystem) == game::subsystem_owner::World)
		{
			// Each node answers with its new effective status, which reaches the players.
			std::scoped_lock scopedLock{ m_worldsMutex };
			for (const auto& world : m_worlds)
			{
				if (world->IsAuthenticated())
				{
					world->SendSetSubsystemEnabled(subsystem, enabled);
				}
			}
			return true;
		}

		if (playerManager.GetSubsystemTable().SetRealmOwned(subsystem, enabled))
		{
			const SubsystemStatusList changed{ { subsystem, enabled ? game::subsystem_status::Available : game::subsystem_status::Unavailable } };
			playerManager.ForEachPlayer([&changed](Player& player)
			{
				if (player.GetWorld())
				{
					player.SendSubsystemStatus(changed);
				}
			});
		}
		return true;
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
