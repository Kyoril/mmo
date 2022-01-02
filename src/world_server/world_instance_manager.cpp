// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "world_instance_manager.h"
#include "regular_update.h"

#include "base/clock.h"
#include "base/timer_queue.h"

#include <algorithm>

namespace mmo
{
	WorldInstanceManager::WorldInstanceManager(asio::io_context& ioContext)
		: m_updateTimer(ioContext)
		, m_lastTick(GetAsyncTimeMs())
	{
		ScheduleNextUpdate();
	}

	WorldInstance& WorldInstanceManager::CreateInstance(MapId mapId)
	{
		std::unique_lock lock{ m_worldInstanceMutex };
		const auto createdInstance = m_worldInstances.emplace_back(std::make_unique<WorldInstance>(*this, mapId)).get();

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
