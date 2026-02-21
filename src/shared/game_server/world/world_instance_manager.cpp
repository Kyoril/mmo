// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_instance_manager.h"
#include "regular_update.h"

#include "base/clock.h"
#include "base/timer_queue.h"

#include <algorithm>

#include "solid_visibility_grid.h"
#include "tiled_unit_finder.h"
#include "tiled_unit_finder_tile.h"
#include "log/default_log_levels.h"

namespace mmo
{
	WorldInstanceManager::WorldInstanceManager(asio::io_context& ioContext,
		Universe& universe,
		const proto::Project& project, 
		IdGenerator<uint64>& objectIdGenerator, ITriggerHandler& triggerHandler, const ConditionMgr& conditionMgr)
		: m_universe(universe)
		, m_project(project)
		, m_objectIdGenerator(objectIdGenerator)
		, m_conditionMgr(conditionMgr)
		, m_updateTimer(ioContext)
		, m_lastTick(GetAsyncTimeMs())
		, m_triggerHandler(triggerHandler)
	{
		ScheduleNextUpdate();
	}

	WorldInstance& WorldInstanceManager::CreateInstance(MapId mapId)
	{
		constexpr int32 maxWorldSize = 64;

		std::unique_lock lock{ m_worldInstanceMutex };
		const auto createdInstance = m_worldInstances.emplace_back(std::make_unique<WorldInstance>(*this, m_universe, m_objectIdGenerator, m_project, mapId,
			std::make_unique<SolidVisibilityGrid>(makeVector(maxWorldSize, maxWorldSize)),
			std::make_unique<TiledUnitFinder>(33.3333f),
			m_triggerHandler, m_conditionMgr)).get();

		instanceCreated(createdInstance->GetId());

		return *createdInstance;
	}

	WorldInstance* WorldInstanceManager::GetInstanceById(InstanceId instanceId)
	{
		const auto it = std::find_if(m_worldInstances.begin(), m_worldInstances.end(), [instanceId](const std::unique_ptr<WorldInstance>& instance)
		{
			return instance->GetId() == instanceId;
		});

		if (it == m_worldInstances.end())
		{
			return nullptr;
		}

		return it->get();
	}

	WorldInstance* WorldInstanceManager::GetInstanceByMap(MapId mapId)
	{
		const auto it = std::find_if(m_worldInstances.begin(), m_worldInstances.end(), [mapId](const std::unique_ptr<WorldInstance>& instance)
		{
			return instance->GetMapId() == mapId;
		});

		if (it == m_worldInstances.end())
		{
			return nullptr;
		}

		return it->get();
	}

	void WorldInstanceManager::DestroyInstance(InstanceId instanceId)
	{
		std::unique_lock lock{ m_worldInstanceMutex };

		const auto it = std::find_if(m_worldInstances.begin(), m_worldInstances.end(), [&instanceId](const std::unique_ptr<WorldInstance>& instance)
		{
			return instance->GetId() == instanceId;
		});

		if (it == m_worldInstances.end())
		{
			WLOG("Tried to destroy instance " << instanceId << " but it was not found");
			return;
		}

		ILOG("Destroying empty dungeon instance " << instanceId << " for map " << (*it)->GetMapId());
		m_emptyDungeonTimestamps.erase(instanceId);

		// Fire signal before erasing (so listeners can react while the instance still exists)
		instanceDestroyed(instanceId);
		m_worldInstances.erase(it);
	}

	void WorldInstanceManager::CheckEmptyDungeonInstances()
	{
		const auto now = GetAsyncTimeMs();

		// Collect instance ids that need to be destroyed (can't destroy while iterating)
		std::vector<InstanceId> toDestroy;

		for (const auto& worldInstance : m_worldInstances)
		{
			// Only track non-persistent (dungeon/raid/etc.) instances
			if (worldInstance->IsPersistent())
			{
				continue;
			}

			const InstanceId id = worldInstance->GetId();
			const bool hasPlayers = worldInstance->HasPlayers();

			if (!hasPlayers)
			{
				// Instance is empty - start or check timer
				const auto emptyIt = m_emptyDungeonTimestamps.find(id);
				if (emptyIt == m_emptyDungeonTimestamps.end())
				{
					// First time this instance is empty - start tracking
					m_emptyDungeonTimestamps[id] = now;
					DLOG("Dungeon instance " << id << " is now empty, starting " << (EmptyDungeonTimeout / 60000) << " minute countdown");
				}
				else if (now - emptyIt->second >= EmptyDungeonTimeout)
				{
					// Timeout reached - mark for destruction
					toDestroy.push_back(id);
				}
			}
			else
			{
				// Instance has players - remove from empty tracking
				m_emptyDungeonTimestamps.erase(id);
			}
		}

		// Destroy empty instances (outside the iteration loop)
		// Need to unlock and use the public method which takes its own lock
		for (const auto& id : toDestroy)
		{
			// Erase directly since we already hold the lock
			const auto it = std::find_if(m_worldInstances.begin(), m_worldInstances.end(), [&id](const std::unique_ptr<WorldInstance>& instance)
			{
				return instance->GetId() == id;
			});

			if (it != m_worldInstances.end())
			{
				ILOG("Destroying empty dungeon instance " << id << " for map " << (*it)->GetMapId() << " after " << (EmptyDungeonTimeout / 60000) << " minutes of inactivity");
				m_emptyDungeonTimestamps.erase(id);
				instanceDestroyed(id);
				m_worldInstances.erase(it);
			}
		}
	}
	
	void WorldInstanceManager::OnUpdate()
	{
		const auto timestamp = GetAsyncTimeMs();
		const auto deltaSeconds = static_cast<float>(timestamp - m_lastTick) / 1000.0f;
		m_lastTick = timestamp;
		
		const RegularUpdate update{ timestamp, deltaSeconds };
		Update(update);
		
		ScheduleNextUpdate();
	}

	void WorldInstanceManager::Update(const RegularUpdate& update)
	{
		std::unique_lock lock{ m_worldInstanceMutex };
		for (const auto& worldInstance : m_worldInstances)
		{
			worldInstance->Update(update);
		}

		// Check for empty dungeon instances that should be cleaned up
		CheckEmptyDungeonInstances();
	}

	void WorldInstanceManager::ScheduleNextUpdate()
	{
		m_updateTimer.expires_from_now(std::chrono::milliseconds(30));
		m_updateTimer.async_wait([this](const asio::error_code& error) { if (!error) OnUpdate(); });
	}
}
