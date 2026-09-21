// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "world_instance.h"
#include "game/game.h"

#include "asio.hpp"

#include <memory>
#include <vector>
#include <mutex>
#include <map>

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
			IdGenerator<uint64>& objectIdGenerator, ITriggerHandler& triggerHandler, const ConditionMgr& conditionMgr);

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

		/// Destroys a world instance by its id. Fires the instanceDestroyed signal.
		/// @param instanceId The id of the instance to destroy.
		void DestroyInstance(InstanceId instanceId);

		/// Sets the game time of day of every hosted instance and of every instance created later.
		///
		/// The realm is the authority on the time of day; this is called whenever it tells this
		/// world node what time it is. The time is kept as an offset to this machine's system time
		/// of day, so it keeps running on its own afterwards.
		/// @param timeOfDay Time of day in milliseconds since midnight.
		/// @param transitionMs How long clients should blend towards the new time, 0 = instantly.
		void SetTimeOfDay(GameTime timeOfDay, uint32 transitionMs);

		/// Gets the current game time of day in milliseconds since midnight. Until the realm sends
		/// a time of day, this is the system time of day.
		[[nodiscard]] GameTime GetTimeOfDay() const;

		/// Stops the world update tick.
		///
		/// The tick re-arms itself every 30ms, so it is permanently outstanding io_context work.
		/// A shutdown that waits for the context to drain never finishes while it is running --
		/// this is what ends it.
		void Stop();

	private:
		void OnUpdate();

		void Update(const RegularUpdate& update);

		/// Checks for empty dungeon instances and destroys them after the timeout.
		void CheckEmptyDungeonInstances();

		void ScheduleNextUpdate();

	private:
		Universe& m_universe;
		const proto::Project& m_project;
		IdGenerator<uint64>& m_objectIdGenerator;
		const ConditionMgr& m_conditionMgr;

		typedef std::vector<std::unique_ptr<WorldInstance>> WorldInstances;
		asio::high_resolution_timer m_updateTimer;

		/// Set by Stop(). Keeps ScheduleNextUpdate from re-arming the tick during shutdown.
		bool m_stopped = false;

		WorldInstances m_worldInstances;

		GameTime m_lastTick;
		std::mutex m_worldInstanceMutex;

		ITriggerHandler& m_triggerHandler;

		/// Offset from this machine's system time of day to the game time of day, in [0, one day).
		GameTime m_timeOfDayOffset = 0;

		/// Tracks when dungeon instances became empty (instanceId -> timestamp).
		std::map<InstanceId, GameTime> m_emptyDungeonTimestamps;

		/// How long an empty dungeon instance survives before being destroyed (in milliseconds).
		static constexpr GameTime EmptyDungeonTimeout = 15 * 60 * 1000; // 15 minutes
	};
}
