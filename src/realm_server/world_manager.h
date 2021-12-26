// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "game/game.h"

#include <memory>
#include <mutex>
#include <list>

namespace mmo
{
	class World;

	/// Manages all connected players.
	class WorldManager final : public NonCopyable
	{
	public:

		typedef std::list<std::shared_ptr<World>> Worlds;

	public:

		/// Initializes a new instance of the player manager class.
		/// @param playerCapacity The maximum number of connections that can be connected at the same time.
		explicit WorldManager(
		    size_t playerCapacity
		);

		~WorldManager() override;

	public:

		/// Notifies the manager that a player has been disconnected which will
		/// delete the world instance.
		void WorldDisconnected(World &player);

		/// Determines whether the player capacity limit has been reached.
		bool HasCapacityBeenReached();

		/// Adds a new player instance to the manager.
		void AddWorld(std::shared_ptr<World> added);

		/// Tries to find a world node which is capable of hosting the given map id and, if provided,
		///	is also hosting the given instance id.
		std::weak_ptr<World> GetIdealWorldNode(MapId mapId, InstanceId instanceId);

	private:

		Worlds m_worlds;
		size_t m_capacity;
		std::mutex m_worldsMutex;
	};
}
