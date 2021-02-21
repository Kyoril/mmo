
#pragma once

#include "base/non_copyable.h"

#include "world_instance.h"
#include "base/typedefs.h"

#include "asio/high_resolution_timer.hpp"

#include <memory>
#include <vector>
#include <mutex>

namespace mmo
{
	class RegularUpdate;
	class TimerQueue;
	
	class WorldInstanceManager : public NonCopyable
	{
	public:
		WorldInstanceManager(asio::io_context& ioContext);

	public:
		/// Creates a new world instance using a specific map id.
		/// @param mapId The map id that is hosted.
		WorldInstance& CreateInstance(uint32 mapId);

	private:
		void OnUpdate();

		void Update(const RegularUpdate& update);

		void ScheduleNextUpdate();

	private:
		typedef std::vector<std::unique_ptr<WorldInstance>> WorldInstances;
		asio::high_resolution_timer m_updateTimer;
		WorldInstances m_worldInstances;
		GameTime m_lastTick;
		std::mutex m_worldInstanceMutex;
	};
}
