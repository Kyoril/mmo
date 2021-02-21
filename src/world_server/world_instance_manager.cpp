
#include "world_instance_manager.h"
#include "regular_update.h"

#include "base/clock.h"
#include "base/timer_queue.h"

namespace mmo
{
	WorldInstanceManager::WorldInstanceManager(asio::io_context& ioContext)
		: m_updateTimer(ioContext)
		, m_lastTick(GetAsyncTimeMs())
	{
		ScheduleNextUpdate();
	}

	WorldInstance& WorldInstanceManager::CreateInstance(uint32 mapId)
	{
		std::unique_lock<std::mutex> lock{ m_worldInstanceMutex };
		return *m_worldInstances.emplace_back(std::make_unique<WorldInstance>(*this));
	}

	void WorldInstanceManager::OnUpdate()
	{
		const auto timestamp = GetAsyncTimeMs();
		const float deltaTime = static_cast<float>(m_lastTick - timestamp) / 30.0f;
		
		const RegularUpdate update{ timestamp, deltaTime };
		Update(update);

		ScheduleNextUpdate();
	}

	void WorldInstanceManager::Update(const RegularUpdate& update)
	{
		std::unique_lock<std::mutex> lock{ m_worldInstanceMutex };
		for (auto& worldInstance : m_worldInstances)
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
