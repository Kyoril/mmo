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
	class MapData
	{
	public:
		virtual ~MapData() = default;

		virtual bool CalculatePath(const Vector3& start, const Vector3& destination, std::vector<Vector3>& out_path) const = 0;
	};

	class SimpleMapData final : public MapData
	{
	public:
		~SimpleMapData() override = default;

		bool CalculatePath(const Vector3& start, const Vector3& destination, std::vector<Vector3>& out_path) const override
		{
			out_path.push_back(start);
			out_path.push_back(destination);
			return true;
		}
	};

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

		std::shared_ptr<GameCreatureS> CreateCreature(const proto::UnitEntry& entry, const Vector3& position, float o, float randomWalkRadius) const;

		MapData* GetMapData() const { return m_mapData.get(); }

		/// Creates a temporary creature that the world instance will also keep a strong reference to. The creature will not be spawned and thus
		///	needs to be spawned using the AddGameObject method.
		std::shared_ptr<GameCreatureS> CreateTemporaryCreature(const proto::UnitEntry& entry, const Vector3& position, float o, float randomWalkRadius);

		/// Removes the reference to a creature that was created using CreateTemporaryCreature. The creature needs to be despawned before this call.
		void DestroyTemporaryCreature(uint64 guid);

	protected:

		void UpdateObject(GameObjectS& object);

		void OnObjectMoved(GameObjectS& object, const MovementInfo& oldMovementInfo);

	private:
		Universe& m_universe;
		IdGenerator<uint64>& m_objectIdGenerator;
		WorldInstanceManager& m_manager;
		InstanceId m_id;
		MapId m_mapId;
		std::unique_ptr<MapData> m_mapData{nullptr};
		const proto::Project& m_project;
		const proto::MapEntry* m_mapEntry{nullptr};
		volatile bool m_updating { false };
		std::unordered_set<GameObjectS*> m_objectUpdates;
		std::unordered_set<GameObjectS*> m_queuedObjectUpdates;
		std::unique_ptr<VisibilityGrid> m_visibilityGrid;

		std::map<uint64, std::shared_ptr<GameCreatureS>> m_temporaryCreatures;

		typedef std::unordered_map<uint64, GameObjectS*> GameObjectsByGuid;
		GameObjectsByGuid m_objectsByGuid;

		typedef std::vector<std::unique_ptr<CreatureSpawner>> CreatureSpawners;
		CreatureSpawners m_creatureSpawners;
		std::map<String, CreatureSpawner*> m_creatureSpawnsByName;
	};
}
