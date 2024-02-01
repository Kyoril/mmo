// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <map>
#include <unordered_set>
#include <unordered_map>

#include "creature_spawner.h"
#include "game/game.h"
#include "visibility_grid.h"
#include "base/id_generator.h"

namespace mmo
{
	class Universe;

	namespace proto
	{
		class Project;
		class MapEntry;
	}

	class MovementInfo;
	class GameObjectS;
	class WorldInstanceManager;
	class RegularUpdate;
	class VisibilityGrid;
	
	/// Represents a single world instance at the world server.
	class WorldInstance
	{
	public:
		explicit WorldInstance(WorldInstanceManager& manager, Universe& universe, IdGenerator<uint64>& objectIdGenerator, const proto::Project& project, MapId mapId, std::unique_ptr<VisibilityGrid> visibilityGrid);
	
	public:
		
		/// Called to update the world instance once every tick.
		void Update(const RegularUpdate& update);
		
		/// Gets the id of this world instance.
		[[nodiscard]] InstanceId GetId() const noexcept { return m_id; }
		
		/// Gets the map id of this world instance.
		[[nodiscard]] MapId GetMapId() const noexcept { return m_mapId; }

		Universe& GetUniverse() const noexcept { return m_universe; }

		WorldInstanceManager& GetManager() const noexcept { return m_manager; }

		/// Adds a game object to this world instance.
		void AddGameObject(GameObjectS &added);

		/// Removes a specific game object from this world.
		void RemoveGameObject(GameObjectS &remove);

		// Not thread safe
		void AddObjectUpdate(GameObjectS& object);
		
		// Not thread safe
		void RemoveObjectUpdate(GameObjectS& object);

		void FlushObjectUpdate(uint64 guid);

		GameObjectS* FindObjectByGuid(uint64 guid);

		VisibilityGrid& GetGrid() const;

		void NotifyObjectMoved(GameObjectS& object, const MovementInfo& previousMovementInfo, const MovementInfo& newMovementInfo);

		std::shared_ptr<GameCreatureS> SpawnCreature(const proto::UnitEntry& entry, Vector3 position, float o, float randomWalkRadius);

	protected:

		void UpdateObject(GameObjectS& object);

		void OnObjectMoved(GameObjectS& object, const MovementInfo& oldMovementInfo);

	private:
		Universe& m_universe;
		IdGenerator<uint64>& m_objectIdGenerator;
		WorldInstanceManager& m_manager;
		InstanceId m_id;
		MapId m_mapId;
		const proto::Project& m_project;
		const proto::MapEntry* m_mapEntry{nullptr};
		volatile bool m_updating { false };
		std::unordered_set<GameObjectS*> m_objectUpdates;
		std::unordered_set<GameObjectS*> m_queuedObjectUpdates;
		std::unique_ptr<VisibilityGrid> m_visibilityGrid;


		typedef std::unordered_map<uint64, GameObjectS*> GameObjectsByGuid;
		GameObjectsByGuid m_objectsByGuid;

		typedef std::vector<std::unique_ptr<CreatureSpawner>> CreatureSpawners;
		CreatureSpawners m_creatureSpawners;
		std::map<String, CreatureSpawner*> m_creatureSpawnsByName;
	};
}
