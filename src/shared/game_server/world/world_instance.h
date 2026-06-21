// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <map>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <memory>

#include "creature_spawner.h"
#include "unit_finder.h"
#include "game/game.h"
#include "game/game_time_component.h"
#include "visibility_grid.h"
#include "world_object_spawner.h"
#include "base/id_generator.h"
#include "base/countdown.h"
#include "shared/proto_data/maps.pb.h"
#include "shared/proto_data/trigger_helper.h"

#include "nav_mesh/map.h"

namespace mmo
{
	class ConditionMgr;
	class ITriggerHandler;
	class LuaScriptMgr;

	namespace proto
	{
		class TriggerEntry;
	}
}

namespace mmo
{
	class MapData
	{
	public:
		virtual ~MapData() = default;

		/// @brief Checks whether posA can see posB without obstruction.
		virtual bool IsInLineOfSight(const Vector3& posA, const Vector3& posB) = 0;

		/// @brief Like IsInLineOfSight but also reports the hit position when blocked.
		/// @param posA Source position.
		/// @param posB Destination position.
		/// @param hitPoint Set to the obstruction point when returning false, or to posB when returning true.
		/// @return true when the ray reaches posB unobstructed.
		virtual bool IsInLineOfSightEx(const Vector3& posA, const Vector3& posB, Vector3& hitPoint) = 0;

		virtual bool CalculatePath(const Vector3& start, const Vector3& destination, std::vector<Vector3>& out_path) const = 0;

		virtual bool FindRandomPointAroundCircle(const Vector3& centerPosition, float radius, Vector3& randomPoint) const = 0;

		/// @brief Gets the water surface height at the given world position, if water is present.
		/// @param pos World position to query (only X/Z are used).
		/// @param outSurfaceY Receives the water surface height (Y) when water is present.
		/// @return True when there is water at the position, false otherwise.
		virtual bool GetWaterSurface(const Vector3& pos, float& outSurfaceY) const = 0;

		/// @brief Returns true if server-side water data is loaded for this map. When false, swim
		/// validation is skipped (the server trusts the client) rather than rejecting all swimming.
		virtual bool IsWaterDataAvailable() const = 0;
	};

	class SimpleMapData final : public MapData
	{
	public:
		~SimpleMapData() override = default;

		bool IsInLineOfSight(const Vector3& posA, const Vector3& posB) override;

		bool IsInLineOfSightEx(const Vector3& posA, const Vector3& posB, Vector3& hitPoint) override
		{
			hitPoint = posB;
			return true;
		}

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

		bool GetWaterSurface(const Vector3& /*pos*/, float& /*outSurfaceY*/) const override
		{
			return false;
		}

		bool IsWaterDataAvailable() const override
		{
			return false;
		}
	};

	class ServerCollisionMap;
	class ServerWaterMap;

	class NavMapData final : public MapData
	{
	public:
		explicit NavMapData(const proto::MapEntry& mapEntry);
		~NavMapData() override;

		bool IsInLineOfSight(const Vector3& posA, const Vector3& posB) override;

		bool IsInLineOfSightEx(const Vector3& posA, const Vector3& posB, Vector3& hitPoint) override;

		bool CalculatePath(const Vector3& start, const Vector3& destination, std::vector<Vector3>& out_path) const override;

		bool FindRandomPointAroundCircle(const Vector3& centerPosition, float radius, Vector3& randomPoint) const override;

		bool GetWaterSurface(const Vector3& pos, float& outSurfaceY) const override;

		bool IsWaterDataAvailable() const override;

	private:
		std::shared_ptr<nav::Map> m_map;
		/// Optional geometry-based collision map for accurate wall/object LOS.
		/// Null when no world file was found or loading failed; nav mesh LOS is used as fallback.
		std::unique_ptr<ServerCollisionMap> m_collisionMap;
		/// Optional terrain water data for swim validation. Null when the map has no water.
		std::unique_ptr<ServerWaterMap> m_waterMap;
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

		/// @brief Finds a world object spawner by its name.
		/// @param name The name to search for.
		/// @return Pointer to the spawner, or nullptr if not found.
		WorldObjectSpawner* FindObjectSpawner(const String& name);

		/// @brief Returns the Lua script manager, if set.
		/// @return Pointer to the script manager, or nullptr if not set.
		LuaScriptMgr* GetScriptMgr() const { return m_scriptMgr; }

		/// @brief Sets the Lua script manager for this world instance.
		/// @param scriptMgr Pointer to the script manager.
		void SetScriptMgr(LuaScriptMgr* scriptMgr) { m_scriptMgr = scriptMgr; }

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

		/// Iterates all game objects currently in this world instance.
		///	@param callback Callable receiving `GameObjectS&`; return false to stop early.
		template<typename Callback>
		void ForEachObject(Callback&& callback) const
		{
			for (const auto& [guid, obj] : m_objectsByGuid)
			{
				if (!callback(*obj))
				{
					break;
				}
			}
		}

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

		/// Sets the state of a named encounter slot for this instance.
		void SetEncounterState(uint32 slotId, uint32 state);

		/// Gets the current state of a named encounter slot.
		uint32 GetEncounterState(uint32 slotId) const;

		/// Sets an instance-scoped variable (counter) for this world instance.
		/// @param key The variable key.
		/// @param value The new value.
		void SetInstanceVariable(uint32 key, int64 value);

		/// Gets the value of an instance-scoped variable, or 0 if it was never set.
		/// @param key The variable key.
		/// @return The variable value, or 0 if unset.
		int64 GetInstanceVariable(uint32 key) const;

		/// @brief Gets the number of players currently in this instance.
		uint32 GetPlayerCount() const { return m_playerCount; }

		/// Raises an instance-owned (map-global) trigger event. Used for events that originate outside
		/// of the standard player enter/leave flow, such as a summoned creature death for an
		/// ownerless instance trigger.
		/// @param eventType The event type to raise.
		/// @param triggeringUnit The unit that raised the event, or nullptr.
		void RaiseInstanceTrigger(trigger_event::Type eventType, GameUnitS* triggeringUnit)
		{
			FireInstanceTriggerEvent(eventType, triggeringUnit);
		}

	protected:

		void UpdateObject(GameObjectS& object) const;

		void OnObjectMoved(GameObjectS& object, const MovementInfo& oldMovementInfo) const;

	private:
		void FireInstanceTriggerEvent(trigger_event::Type eventType, GameUnitS* triggeringUnit);

		/// Fires a specific instance trigger only if it listens for the given event (with optional data match).
		/// @param eventType The event type to match.
		/// @param triggeringUnit The unit that raised the event, or nullptr.
		/// @param eventData Optional data values to match against the event's data (e.g. encounter slot/state).
		void FireInstanceTriggerEvent(trigger_event::Type eventType, GameUnitS* triggeringUnit, const std::vector<uint32>& eventData);

		/// (Re)builds and starts the repeating OnTimer timers for instance-owned (map) triggers.
		void StartInstanceTimers();

		/// Stops and clears all instance-owned OnTimer timers.
		void StopInstanceTimers();

		/// (Re)schedules the next firing of an instance OnTimer timer based on its interval data.
		void ScheduleInstanceTimer(Countdown& countdown, const proto::TriggerEntry& entry);

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

		LuaScriptMgr* m_scriptMgr = nullptr;

		/// Per-instance encounter slot states. Key = slot id, value = encounter_state::Type value.
		std::unordered_map<uint32, uint32> m_encounterStates;

		/// Per-instance script variables/counters owned by the world instance. Key = variable key.
		std::unordered_map<uint32, int64> m_instanceVariables;

		/// Active repeating OnTimer timers for instance-owned (map) triggers.
		std::vector<std::unique_ptr<Countdown>> m_instanceTimers;

		/// Number of players currently in this instance (tracked for wipe detection).
		uint32 m_playerCount{ 0 };
	};
}
