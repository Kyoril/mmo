// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game/subsystem.h"

#include "base/non_copyable.h"
#include "game/game.h"

#include <memory>
#include <mutex>
#include <list>

namespace mmo
{
	class PlayerManager;
	class World;

	/// Manages all connected world nodes.
	///
	/// **Threading:** single-threaded, exactly as PlayerManager -- see the note there before
	/// changing the realm server's thread count.
	class WorldManager final : public NonCopyable
	{
	public:

		typedef std::list<std::shared_ptr<World>> Worlds;

	public:

		/// Initializes a new instance of the world manager class.
		/// @param playerCapacity The maximum number of world nodes that can be connected at the same time.
		explicit WorldManager(
		    size_t playerCapacity
		);

		~WorldManager() override;

	public:

		/// Notifies the manager that a world node has been disconnected which will
		/// delete the world instance.
		void WorldDisconnected(World &player);

		/// Determines whether the world capacity limit has been reached.
		bool HasCapacityBeenReached();

		/// Adds a new world node instance to the manager.
		void AddWorld(std::shared_ptr<World> added);

		/// Tries to find a world node which is capable of hosting the given map id and, if provided,
		///	is also hosting the given instance id.
		std::shared_ptr<World> GetIdealWorldNode(MapId mapId, InstanceId instanceId);

		/// Disconnects every managed world node. Used at shutdown. See
		/// PlayerManager::DisconnectAll for why this is a direct call.
		void DisconnectAll();

		/// Gets a world node by instance id.
		std::shared_ptr<World> GetWorldByInstanceId(InstanceId instanceId);

		/// Sends the realm-wide time of day to every authenticated world node.
		/// @param timeOfDay Time of day in milliseconds since midnight.
		/// @param transitionMs How long clients should blend towards the new time, 0 = instantly.
		void BroadcastTimeOfDay(GameTime timeOfDay, uint32 transitionMs);

		/// Tells every connected world node to shut down for good (scheduled realm shutdown).
		void BroadcastShutdown();

		/// Number of currently connected world nodes.
		size_t GetWorldCount();

		/// Switches a subsystem realm-wide: world-owned subsystems are relayed to every world node,
		/// realm-owned ones are set in the realm's table and pushed to every player in the world.
		/// @returns false if the subsystem id is unknown.
		bool SetSubsystemEnabled(game::Subsystem subsystem, bool enabled, PlayerManager& playerManager);

	private:

		Worlds m_worlds;
		size_t m_capacity;
		std::mutex m_worldsMutex;
	};
}
