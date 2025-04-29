// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_instance_manager.h"
#include "regular_update.h"

#include "base/clock.h"
#include "base/timer_queue.h"

#include <algorithm>

#include "solid_visibility_grid.h"
#include "tiled_unit_finder.h"
#include "tiled_unit_finder_tile.h"

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
	}

	void WorldInstanceManager::ScheduleNextUpdate()
	{
		m_updateTimer.expires_from_now(std::chrono::milliseconds(30));
		m_updateTimer.async_wait([this](const asio::error_code& error) { if (!error) OnUpdate(); });
	}
}
