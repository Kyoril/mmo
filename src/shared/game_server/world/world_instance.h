// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <map>
#include <unordered_set>
#include <unordered_map>

#include "creature_spawner.h"
#include "unit_finder.h"
#include "game/game.h"
#include "game/game_time_component.h"
#include "visibility_grid.h"
#include "world_object_spawner.h"
#include "base/id_generator.h"
#include "shared/proto_data/maps.pb.h"

#include "nav_mesh/map.h"

namespace mmo
{
	class ConditionMgr;
	class ITriggerHandler;
}

namespace mmo
{
	class MapData
	{
	public:
		virtual ~MapData() = default;

		virtual bool IsInLineOfSight(const Vector3& posA, const Vector3& posB) = 0;

		virtual bool CalculatePath(const Vector3& start, const Vector3& destination, std::vector<Vector3>& out_path) const = 0;

		virtual bool FindRandomPointAroundCircle(const Vector3& centerPosition, float radius, Vector3& randomPoint) const = 0;
	};

	class SimpleMapData final : public MapData
	{
	public:
		~SimpleMapData() override = default;

		bool IsInLineOfSight(const Vector3& posA, const Vector3& posB) override;

		bool CalculatePath(const Vector3& start, const Vector3& destination, std::vector<Vector3>& out_path) const override
		{
			out_path.push_back(start);
			out_path.push_back(destination);
			return true;
		}

		bool FindRandomPointAroundCircle(const Vector3& centerPosition, float radius, Vector3& randomPoint) const override
		{
			const float x = fmod(rand(), (radius * 2)) - radius;
			const float z = fmod(rand(), (radius * 2)) - radius;

			randomPoint = centerPosition + Vector3(x, 0.0f, z);

			return true;
		}
	};

	class NavMapData final : public MapData
	{
	public:
		explicit NavMapData(const proto::MapEntry& mapEntry);

		bool IsInLineOfSight(const Vector3& posA, const Vector3& posB) override;

		bool CalculatePath(const Vector3& start, const Vector3& destination, std::vector<Vector3>& out_path) const override;

		bool FindRandomPointAroundCircle(const Vector3& centerPosition, float radius, Vector3& randomPoint) const override;

	private:
		std::shared_ptr<nav::Map> m_map;
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
		explicit WorldInstance(WorldInstanceManager& manager, Universe& universe, IdGenerator<uint64>& objectIdGenerator, const proto::Project& project, MapId mapId, std::unique_ptr<VisibilityGrid> visibilityGrid, std::unique_ptr<UnitFinder> unitFinder, ITriggerHandler& triggerHandler, const ConditionMgr& conditionMgr);
	
	public:
		
		/// Called to update the world instance once every tick.
		void Update(const RegularUpdate& update);
		
		/// Gets the id of this world instance.
		[[nodiscard]] InstanceId GetId() const { return m_id; }
		
		/// Gets the map id of this world instance.
		[[nodiscard]] MapId GetMapId() const { return m_mapId; }

		Universe& GetUniverse() const { return m_universe; }

		WorldInstanceManager& GetManager() const { return m_manager; }

		/// Adds a game object to this world instance.
		void AddGameObject(GameObjectS &added);

		/// Removes a specific game object from this world.
		void RemoveGameObject(GameObjectS &remove);

		// Not thread safe
		void AddObjectUpdate(GameObjectS& object);
		
		// Not thread safe
		void RemoveObjectUpdate(GameObjectS& object);

		void FlushObjectUpdate(uint64 guid);

		UnitFinder& GetUnitFinder() { return *m_unitFinder; }

		GameObjectS* FindObjectByGuid(uint64 guid);

		///
		CreatureSpawner* FindCreatureSpawner(const String& name);

		///
		//WorldObjectSpawner* FindObjectSpawner(const String& name);

		template<class T>
		T* FindByGuid(uint64 guid)
		{
			return dynamic_cast<T*>(FindObjectByGuid(guid));
		}

		VisibilityGrid& GetGrid() const;

		void NotifyObjectMoved(GameObjectS& object, const MovementInfo& previousMovementInfo, const MovementInfo& newMovementInfo) const;

		std::shared_ptr<GameCreatureS> CreateCreature(const proto::UnitEntry& entry, const Vector3& position, float o, float randomWalkRadius);

		std::shared_ptr<GameWorldObjectS> SpawnWorldObject(const proto::ObjectEntry& entry, const Vector3& position);

		MapData* GetMapData() const { return m_mapData.get(); }

		/// Creates a temporary creature that the world instance will also keep a strong reference to. The creature will not be spawned and thus
		///	needs to be spawned using the AddGameObject method.
		std::shared_ptr<GameCreatureS> CreateTemporaryCreature(const proto::UnitEntry& entry, const Vector3& position, float o, float randomWalkRadius);

		/// Removes the reference to a creature that was created using CreateTemporaryCreature. The creature needs to be despawned before this call.
		void DestroyTemporaryCreature(uint64 guid);

		bool IsDungeon() const { return m_mapEntry->instancetype() == proto::MapEntry_MapInstanceType_DUNGEON; }

		bool IsRaid() const { return m_mapEntry->instancetype() == proto::MapEntry_MapInstanceType_RAID; }

		bool IsInstancedPvE() const { return IsDungeon() || IsRaid(); }

		bool IsPersistent() const { return m_mapEntry->instancetype() == proto::MapEntry_MapInstanceType_GLOBAL; }

		bool IsArena() const { return m_mapEntry->instancetype() == proto::MapEntry_MapInstanceType_ARENA; }

		bool IsBattleground() const { return m_mapEntry->instancetype() == proto::MapEntry_MapInstanceType_BATTLEGROUND; }

		bool IsPvP() const { return IsArena() || IsBattleground(); }

		IdGenerator<uint64>& GetItemIdGenerator() { return m_itemIdGenerator; }
		const ConditionMgr& GetConditionMgr() const { return m_conditionMgr; }

		/// @brief Gets the game time component.
		/// @return Reference to the game time component.
		GameTimeComponent& GetGameTime() { return m_gameTime; }

		/// @brief Gets the game time component.
		/// @return Constant reference to the game time component.
		const GameTimeComponent& GetGameTime() const { return m_gameTime; }

		/// @brief Checks for players in the world instance.
		/// @return True if there are players in the world instance, false otherwise.
		[[nodiscard]] bool HasPlayers() const;
	
		/// @brief Broadcasts the current game time to all players in the world.
		void BroadcastGameTime();

	protected:

		void UpdateObject(GameObjectS& object) const;

		void OnObjectMoved(GameObjectS& object, const MovementInfo& oldMovementInfo) const;
	private:
		Universe& m_universe;
		IdGenerator<uint64>& m_objectIdGenerator;
		IdGenerator<uint64> m_itemIdGenerator;
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
		std::unique_ptr<UnitFinder> m_unitFinder;
		GameTimeComponent m_gameTime;
		
		/// Last time when game time update was broadcast to players
		GameTime m_lastTimeUpdateBroadcast { 0 };

		std::map<uint64, std::shared_ptr<GameCreatureS>> m_temporaryCreatures;
		ITriggerHandler& m_triggerHandler;

		typedef std::unordered_map<uint64, GameObjectS*> GameObjectsByGuid;
		GameObjectsByGuid m_objectsByGuid;

		typedef std::vector<std::unique_ptr<CreatureSpawner>> CreatureSpawners;
		typedef std::vector<std::unique_ptr<WorldObjectSpawner>> ObjectSpawners;
		CreatureSpawners m_creatureSpawners;
		std::map<String, CreatureSpawner*> m_creatureSpawnsByName;
		ObjectSpawners m_objectSpawners;
		std::map<String, WorldObjectSpawner*> m_objectSpawnsByName;

		const ConditionMgr& m_conditionMgr;
	};
}
