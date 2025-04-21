// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "world_instance.h"
#include "game/game.h"

#include "asio.hpp"

#include <memory>
#include <vector>
#include <mutex>

#include "game_server/trigger_handler.h"
#include "base/signal.h"

namespace mmo
{
	class Universe;
	class RegularUpdate;
	class TimerQueue;

	/// Manages active world instances.
	class WorldInstanceManager : public NonCopyable
	{
	public:
		/// Fired whenever a world instance has been created.
		signal<void(InstanceId)> instanceCreated;

		/// Fired whenever a world instance has been destroyed.
		signal<void(InstanceId)> instanceDestroyed;

	public:
		/// Creates a new instance of the WorldInstanceManager class and initializes it.
		///	@param ioContext The global async io context to use.
		explicit WorldInstanceManager(asio::io_context& ioContext,
			Universe& universe, const proto::Project& project,
			IdGenerator<uint64>& objectIdGenerator, ITriggerHandler& triggerHandler);

	public:
		/// Creates a new world instance using a specific map id.
		/// @param mapId The map id that is hosted.
		WorldInstance& CreateInstance(MapId mapId);

		/// Tries to restore the instance.
		WorldInstance& LoadInstance(InstanceId instanceId);

		/// Gets a world instance by it's id.
		WorldInstance* GetInstanceById(InstanceId instanceId);

		/// Gets any world instance by a map id.
		WorldInstance* GetInstanceByMap(MapId mapId);

	private:
		void OnUpdate();

		void Update(const RegularUpdate& update);

		void ScheduleNextUpdate();

	private:
		Universe& m_universe;
		const proto::Project& m_project;
		IdGenerator<uint64>& m_objectIdGenerator;

		typedef std::vector<std::unique_ptr<WorldInstance>> WorldInstances;
		asio::high_resolution_timer m_updateTimer;

		WorldInstances m_worldInstances;

		GameTime m_lastTick;
		std::mutex m_worldInstanceMutex;

		ITriggerHandler& m_triggerHandler;
	};
}
