// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_instance.h"
#include "server_collision_map.h"
#include "server_water_map.h"

#include "game_server/world/creature_spawner.h"
#include "game_server/world/each_tile_in_sight.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/objects/game_world_object_s.h"
#include "game_server/world/world_instance_manager.h"
#include "game_server/world/regular_update.h"
#include "game_server/world/tile_subscriber.h"
#include "game_server/world/universe.h"
#include "log/default_log_levels.h"
#include "game_server/world/visibility_grid.h"
#include "game_server/world/visibility_tile.h"
#include "base/utilities.h"
#include "binary_io/vector_sink.h"
#include "game_server/objects/game_object_s.h"
#include "game_server/world/each_tile_in_region.h"
#include "game_server/trigger_handler.h"
#include "proto_data/project.h"

namespace mmo
{
	namespace update_type
	{
		enum Type
		{
			UpdateValues,
		};
	}

	static TileIndex2D GetObjectTile(const GameObjectS& object, const VisibilityGrid& grid)
	{
		TileIndex2D gridIndex;
		grid.GetTilePosition(object.GetPosition(), gridIndex[0], gridIndex[1]);

		return gridIndex;
	}

	static void CreateValueUpdateBlock(GameObjectS& object, std::vector<std::vector<char>>& out_blocks)
	{
		// Write create object packet
		std::vector<char> createBlock;
		{
			io::VectorSink sink(createBlock);
			io::Writer writer(sink);
			constexpr uint8 updateType = update_type::UpdateValues;

			// Header with object guid and type
			const uint64 guid = object.GetGuid();
			writer
				<< io::write<uint8>(updateType)
				<< io::write_packed_guid(guid);

			// Write values update
			object.WriteValueUpdateBlock(writer, false);
		}

		// Add block
		out_blocks.push_back(createBlock);
	}

	bool SimpleMapData::IsInLineOfSight(const Vector3& posA, const Vector3& posB)
	{
		// No geometry available — treat everything as visible.
		return true;
	}

	NavMapData::NavMapData(const proto::MapEntry& mapEntry)
	{
		m_map = std::make_shared<nav::Map>(mapEntry.directory());

		// Load all map pages
		DLOG("Loading nav map pages...");
		m_map->LoadAllPages();

		// Attempt to load world geometry for accurate 3D LOS. Falls back to nav mesh LOS
		// if no world file is found (e.g. simple outdoor-only maps).
		auto collisionMap = std::make_unique<ServerCollisionMap>(mapEntry.directory());
		if (collisionMap->IsLoaded())
		{
			m_collisionMap = std::move(collisionMap);
			DLOG("NavMapData: using geometry-based LOS for map '" << mapEntry.directory() << "'");
		}
		else
		{
			WLOG("NavMapData: geometry collision unavailable for map '"
				<< mapEntry.directory() << "' — all IsInLineOfSight calls will return true (unblocked)");
		}

		// Load terrain water data for swim validation. Optional — null when the map has no water.
		auto waterMap = std::make_unique<ServerWaterMap>(mapEntry.directory());
		if (waterMap->IsLoaded())
		{
			m_waterMap = std::move(waterMap);
		}
	}

	NavMapData::~NavMapData() = default;

	bool NavMapData::IsInLineOfSight(const Vector3& posA, const Vector3& posB)
	{
		if (!m_collisionMap)
		{
			// No geometry loaded — cannot determine LOS. Treat as unblocked.
			return true;
		}

		// Raise both points to approximate eye level.
		// 1.8 m is used for all unit types as a static approximation;
		// per-unit model height can be substituted here later.
		static const Vector3 eyeOffset(0.f, 1.8f, 0.f);
		return m_collisionMap->LineOfSight(posA + eyeOffset, posB + eyeOffset);
	}

	bool NavMapData::IsInLineOfSightEx(const Vector3& posA, const Vector3& posB, Vector3& hitPoint)
	{
		hitPoint = posB;

		if (!m_collisionMap)
		{
			return true;
		}

		static const Vector3 eyeOffset(0.f, 1.8f, 0.f);
		const bool result = m_collisionMap->LineOfSightEx(posA + eyeOffset, posB + eyeOffset, hitPoint);
		// Lower the reported hit point back to ground level for consistent world-space display.
		hitPoint -= eyeOffset;
		return result;
	}

	bool NavMapData::CalculatePath(const Vector3& start, const Vector3& destination, std::vector<Vector3>& out_path) const
	{
		return m_map->FindPath(start, destination, out_path, true);
	}

	bool NavMapData::FindRandomPointAroundCircle(const Vector3& centerPosition, float radius, Vector3& randomPoint) const
	{
		return m_map->FindRandomPointAroundCircle(centerPosition, radius, randomPoint);
	}

	bool NavMapData::GetWaterSurface(const Vector3& pos, float& outSurfaceY) const
	{
		if (!m_waterMap)
		{
			return false;
		}

		return m_waterMap->GetWaterSurface(pos.x, pos.z, outSurfaceY);
	}

	bool NavMapData::IsWaterDataAvailable() const
	{
		return m_waterMap != nullptr;
	}

	WorldInstance::WorldInstance(WorldInstanceManager& manager, Universe& universe, IdGenerator<uint64>& objectIdGenerator, const proto::Project& project, const MapId mapId, std::unique_ptr<VisibilityGrid> visibilityGrid, std::unique_ptr<UnitFinder> unitFinder, ITriggerHandler& triggerHandler, const ConditionMgr& conditionMgr)
		: m_universe(universe)
		, m_objectIdGenerator(objectIdGenerator)
		, m_manager(manager)
		, m_mapId(mapId)
		, m_project(project)
		, m_visibilityGrid(std::move(visibilityGrid))
		, m_unitFinder(std::move(unitFinder))
		, m_gameTime(0, 1.0f)
		, m_triggerHandler(triggerHandler)
		, m_conditionMgr(conditionMgr) // Initialize with default time (midnight) and normal speed
	{
		uuids::uuid_system_generator generator;
		m_id = generator();

		m_mapEntry = m_project.maps.getById(m_mapId);
		if (!m_mapEntry)
		{
			ELOG("Failed to load map data for map id " << m_mapId << ": Map not found!");
			return;
		}

		// Get current time of day and apply game time
		const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

		// Game time is milliseconds, so calculate total milliseconds of now
		auto nowTime = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
		auto nowGameTime = nowTime % constants::OneDay;
		m_gameTime.SetTime(nowGameTime);

		m_mapData = std::make_unique<NavMapData>(*m_mapEntry);

		// Add object spawners
		for (int i = 0; i < m_mapEntry->objectspawns_size(); ++i)
		{
			// Create a new spawner
			const auto& spawn = m_mapEntry->objectspawns(i);

			const auto* objectEntry = m_project.objects.getById(spawn.objectentry());
			if (!objectEntry)
			{
#ifdef _DEBUG
				WLOG("Unknown object spawn found in map " << m_mapId << ": Object entry " << spawn.objectentry() << " not found!");
#endif
				continue;
			}

			std::unique_ptr<WorldObjectSpawner> spawner(new WorldObjectSpawner(
				*this,
				*objectEntry,
				spawn.maxcount(),
				spawn.respawndelay(),
				Vector3(spawn.location().positionx(), spawn.location().positiony(), spawn.location().positionz()),
				{ spawn.location().rotationw(), spawn.location().rotationx(), spawn.location().rotationy(), spawn.location().rotationz() },
				spawn.radius(),
				spawn.animprogress(),
				spawn.state(),
				spawn.loot_entry()));
			m_objectSpawners.push_back(std::move(spawner));
			if (!spawn.name().empty())
			{
				m_objectSpawnsByName[spawn.name()] = m_objectSpawners.back().get();
			}
		}

		// Add creature spawners
		for (int i = 0; i < m_mapEntry->unitspawns_size(); ++i)
		{
			// Create a new spawner
			const auto& spawn = m_mapEntry->unitspawns(i);

			const auto* unitEntry = m_project.units.getById(spawn.unitentry());
			if (!unitEntry)
			{
#ifdef _DEBUG
				WLOG("Unknown unit spawn found in map " << m_mapId << ": Unit entry " << spawn.unitentry() << " not found!");
#endif
				continue;
			}

			auto spawner = std::make_unique<CreatureSpawner>(
				*this,
				*unitEntry,
				spawn);

			m_creatureSpawners.push_back(std::move(spawner));

			if (!spawn.name().empty())
			{
				m_creatureSpawnsByName[spawn.name()] = m_creatureSpawners.back().get();
			}
		}
	}

	void WorldInstance::Update(const RegularUpdate& update)
	{
		m_updating = true;

		// Update game time
		m_gameTime.Update(update.GetTimestamp());

		// Check if we need to broadcast game time (every 5 minutes of real time)
		constexpr GameTime TimeUpdateInterval = 5 * constants::OneMinute; // Every 5 minutes
		if (update.GetTimestamp() - m_lastTimeUpdateBroadcast >= TimeUpdateInterval)
		{
			if (HasPlayers())
			{
				BroadcastGameTime();
			}
			m_lastTimeUpdateBroadcast = update.GetTimestamp();
		}

		// Create a copy of object updates to iterate over safely
		auto objectsToUpdate = m_objectUpdates;
		for (const auto& object : objectsToUpdate)
		{
			// Ensure the object is still alive and in this world instance
			if (object &&
				object->GetWorldInstance() == this)
			{
				UpdateObject(*object);
			}
		}
		
		m_updating = false;
		m_objectUpdates = m_queuedObjectUpdates;
		m_queuedObjectUpdates.clear();
	}

	void WorldInstance::AddGameObject(GameObjectS& added)
	{
		ASSERT(!m_updating);
		m_objectsByGuid.emplace(added.GetGuid(), &added);

		// No need for visibility updates for item objects
		if (added.GetTypeId() == ObjectTypeId::Item ||
			added.GetTypeId() == ObjectTypeId::Container)
		{
			return;
		}

		const auto& position = added.GetPosition();

		TileIndex2D gridIndex;
		if (!m_visibilityGrid->GetTilePosition(position, gridIndex[0], gridIndex[1]))
		{
			ELOG("Could not resolve grid location!");
			return;
		}
		
		auto &tile = m_visibilityGrid->RequireTile(gridIndex);
		tile.GetGameObjects().add(&added);
		added.SetWorldInstance(this);

	added.spawned(*this);
	
	ForEachTileInSight(
	    *m_visibilityGrid,
	    tile.GetPosition(),
	    [&added](VisibilityTile & tile)
	{
	    const std::vector objects { &added };
	    for (auto *subscriber : tile.GetWatchers())
		{
			if (subscriber->GetGameUnit().GetGuid() == added.GetGuid())
			{
				continue;
			}

			if (added.IsUnit() && !added.AsUnit().CanBeSeenBy(subscriber->GetGameUnit()))
			{
				continue; // Skip subscribers that cannot see this unit
			}

			subscriber->NotifyObjectsSpawned(objects);
		}
	});

	if (const auto addedUnit = dynamic_cast<GameUnitS*>(&added))
	{
		m_unitFinder->AddUnit(*addedUnit);
	}

	// Activate passive spells AFTER spawn notifications have been sent
	// This ensures clients receive the spawn packet before any stat updates from passive effects
	if (const auto unit = dynamic_cast<GameUnitS*>(&added))
	{
		unit->ActivatePassiveSpells();
		added.AsUnit().unitTrigger.connect([this](const proto::TriggerEntry& trigger, GameUnitS& owner, GameUnitS* triggeringUnit) {
			m_triggerHandler.ExecuteTrigger(trigger, TriggerContext(&owner, triggeringUnit), 0);
			});
		}

	if (added.GetTypeId() == ObjectTypeId::Player)
	{
		++m_playerCount;

		// Start instance-wide periodic timers once the first player is present.
		if (m_playerCount == 1)
		{
			StartInstanceTimers();
		}

		FireInstanceTriggerEvent(trigger_event::OnPlayerEnterInstance, nullptr);
	}
}

	void WorldInstance::RemoveGameObject(GameObjectS& remove)
	{
		ASSERT(!m_updating);

		if (const auto removedUnit = dynamic_cast<GameUnitS*>(&remove))
		{
			m_unitFinder->RemoveUnit(*removedUnit);
		}

		const auto it = m_objectsByGuid.find(remove.GetGuid());
		if (it == m_objectsByGuid.end())
		{
			return;
		}

		// Keep the object alive for this call
		auto strong = remove.shared_from_this();
		m_objectsByGuid.erase(it);

		// Clear update
		if (m_queuedObjectUpdates.contains(strong.get()))
		{
			m_queuedObjectUpdates.erase(strong.get());
		}
		if (m_objectUpdates.contains(strong.get()))
		{
			m_objectUpdates.erase(strong.get());
		}

		// No need for visibility updates for item objects
		if (!remove.IsItem() && !remove.IsContainer())
		{
			TileIndex2D gridIndex;
			if (!m_visibilityGrid->GetTilePosition(remove.GetPosition(), gridIndex[0], gridIndex[1]))
			{
				ELOG("Could not resolve grid location for object " << log_hex_digit(remove.GetGuid()) << " during removal — skipping tile cleanup");
			}
			else
			{
				auto* tile = m_visibilityGrid->GetTile(gridIndex);
				if (!tile)
				{
					ELOG("Could not find tile for object " << log_hex_digit(remove.GetGuid()) << " during removal — skipping tile cleanup");
				}
				else
				{
					tile->GetGameObjects().remove(&remove);
					remove.OnDespawn();

					ForEachTileInSight(
						*m_visibilityGrid,
						tile->GetPosition(),
						[&remove](VisibilityTile& tile)
						{
							std::vector objects{ &remove };
							for (auto* subscriber : tile.GetWatchers())
							{
								// Only despawn if we were visible before
								if (remove.IsUnit() && !remove.AsUnit().CanBeSeenBy(subscriber->GetGameUnit()))
								{
									continue; // Skip subscribers that cannot see this unit
								}

								subscriber->NotifyObjectsDespawned(objects);
							}
						});
				}
			}
		}

		if (remove.destroy)
		{
			remove.destroy(remove);
		}

		if (remove.GetTypeId() == ObjectTypeId::Player)
		{
			if (m_playerCount > 0)
			{
				--m_playerCount;
			}

			FireInstanceTriggerEvent(trigger_event::OnPlayerLeaveInstance, nullptr);

			if (m_playerCount == 0)
			{
				// No players left — stop instance-wide periodic timers.
				StopInstanceTimers();

				if (IsInstancedPvE())
				{
					FireInstanceTriggerEvent(trigger_event::OnAllPlayersDead, nullptr);
				}
			}
		}
	}

	void WorldInstance::AddObjectUpdate(GameObjectS& object)
	{
		if (m_updating)
		{
			m_queuedObjectUpdates.emplace(&object);
		}
		else
		{
			m_objectUpdates.emplace(&object);
		}
	}

	void WorldInstance::RemoveObjectUpdate(GameObjectS& object)
	{
		if (m_updating)
		{
			m_queuedObjectUpdates.erase(&object);
		}
		else
		{
			m_objectUpdates.erase(&object);
		}
	}

	void WorldInstance::FlushObjectUpdate(const uint64 guid)
	{
		if (auto* object = FindObjectByGuid(guid))
		{
			UpdateObject(*object);
		}
	}

	GameObjectS* WorldInstance::FindObjectByGuid(const uint64 guid)
	{
		const auto it = m_objectsByGuid.find(guid);
		if (it == m_objectsByGuid.end())
		{
			return nullptr;
		}

		return it->second;
	}

	CreatureSpawner* WorldInstance::FindCreatureSpawner(const String& name)
	{
		const auto it = m_creatureSpawnsByName.find(name);
		if (it == m_creatureSpawnsByName.end())
		{
			return nullptr;
		}

		return it->second;
	}

	WorldObjectSpawner* WorldInstance::FindObjectSpawner(const String& name)
	{
		const auto it = m_objectSpawnsByName.find(name);
		if (it == m_objectSpawnsByName.end())
		{
			return nullptr;
		}

		return it->second;
	}

	VisibilityGrid& WorldInstance::GetGrid() const
	{
		ASSERT(m_visibilityGrid);
		return *m_visibilityGrid;
	}

	void WorldInstance::NotifyObjectMoved(GameObjectS& object, const MovementInfo& previousMovementInfo,
		const MovementInfo& newMovementInfo) const
	{
		OnObjectMoved(object, previousMovementInfo);

		if (GameUnitS* unit = dynamic_cast<GameUnitS*>(&object))
		{
			m_unitFinder->UpdatePosition(*unit, previousMovementInfo.position);
		}
	}

	std::shared_ptr<GameCreatureS> WorldInstance::CreateCreature(const proto::UnitEntry& entry, const Vector3& position, const float o, float randomWalkRadius)
	{
		// Create the unit
		auto spawned = std::make_shared<GameCreatureS>(
			m_project,
			m_universe.GetTimers(),
			entry);

		spawned->ApplyMovementInfo(
			{ movement_flags::None, GetAsyncTimeMs(), position, Radian(o), Radian(0), 0, Vector3::Zero });
		spawned->Initialize();
		spawned->Set<uint64>(object_fields::Guid, CreateEntryGUID(m_objectIdGenerator.GenerateId(), entry.id(), GuidType::Unit));
		
		// TODO: This might be bad because we aren't technically really spawned in this world yet! We do this only so that passives can be cast!
		spawned->SetWorldInstance(this);
		spawned->SetEntry(entry);
		spawned->SetHealthPercent(1.0f);

		return spawned;
	}

	std::shared_ptr<GameWorldObjectS> WorldInstance::SpawnWorldObject(const proto::ObjectEntry& entry, const Vector3& position)
	{
		// Create the object
		auto spawned = std::make_shared<GameWorldObjectS>(m_project, entry);
		spawned->ApplyMovementInfo(
			{ movement_flags::None, GetAsyncTimeMs(), position, Radian(0.0f), Radian(0), 0, Vector3::Zero });

		spawned->Initialize();
		spawned->Set<uint64>(object_fields::Guid, CreateEntryGUID(m_objectIdGenerator.GenerateId(), entry.id(), GuidType::Object));
		spawned->SetWorldInstance(this);

		return std::static_pointer_cast<GameWorldObjectS>(spawned);
	}

	std::shared_ptr<GameCreatureS> WorldInstance::CreateTemporaryCreature(const proto::UnitEntry& entry, const Vector3& position, const float o, const float randomWalkRadius)
	{
		auto creature = CreateCreature(entry, position, o, randomWalkRadius);
		m_temporaryCreatures[creature->GetGuid()] = creature;

		creature->destroy = [this](const GameObjectS& gameObjectS)
			{
				DestroyTemporaryCreature(gameObjectS.GetGuid());
			};

		return std::move(creature);
	}

	void WorldInstance::DestroyTemporaryCreature(const uint64 guid)
	{
		const auto it = m_temporaryCreatures.find(guid);
		if (it == m_temporaryCreatures.end())
		{
			ELOG("Could not find temporary creature with guid " << log_hex_digit(guid));
			return;
		}

		m_temporaryCreatures.erase(it);
	}

	void WorldInstance::UpdateObject(GameObjectS& object) const
	{
		const std::vector objects{ &object };

		// Send updates to all subscribers in sight
		const TileIndex2D center = GetObjectTile(object, *m_visibilityGrid);
		ForEachSubscriberInSight(
			*m_visibilityGrid,
			center,
			[&objects, &object](TileSubscriber& subscriber)
			{
				auto& character = subscriber.GetGameUnit();

				if (object.IsUnit() && !object.AsUnit().CanBeSeenBy(character))
				{
					return; // Skip subscribers that cannot see this unit
				}

				subscriber.NotifyObjectsUpdated(objects);
			});

		object.ClearFieldChanges();
	}

	void WorldInstance::OnObjectMoved(GameObjectS& object, const MovementInfo& oldMovementInfo) const
	{
		// Calculate old tile index
		TileIndex2D oldIndex;
		m_visibilityGrid->GetTilePosition(oldMovementInfo.position, oldIndex[0], oldIndex[1]);

		// Calculate new tile index
		const TileIndex2D newIndex = GetObjectTile(object, *m_visibilityGrid);

		// Check if tile changed
		if (oldIndex != newIndex)
		{
			// Get the tiles
			VisibilityTile* oldTile = m_visibilityGrid->GetTile(oldIndex);
			ASSERT(oldTile);
			VisibilityTile* newTile = m_visibilityGrid->GetTile(newIndex);
			ASSERT(newTile);

			// Remove the object
			oldTile->GetGameObjects().remove(&object);

			const std::vector objects{ &object };

			// Send despawn packets to subscribers that previously knew about this object
			// but whose tile is no longer in sight of the new position.
			// Guard with IsObjectKnown to avoid sending destroy for objects that were
			// never spawned to the client (e.g. units that were invisible while in range).
			ForEachTileInSightWithout(
				*m_visibilityGrid,
				oldTile->GetPosition(),
				newTile->GetPosition(),
				[&object, &objects](VisibilityTile& tile)
				{
					const uint64 guid = object.GetGuid();

					for (auto* subscriber : tile.GetWatchers().getElements())
					{
						if (subscriber->GetGameUnit().GetGuid() == guid)
						{
							continue;
						}

						// Only despawn if the client was actually told about this object.
						if (!subscriber->IsObjectKnown(guid))
						{
							continue;
						}

						subscriber->NotifyObjectsDespawned(objects);
					}
				});

			// Notify watchers about the pending tile change
			object.tileChangePending(*oldTile, *newTile);

			// Send spawn packets to subscribers whose tile newly comes into sight.
			// Mirror the CanBeSeenBy guard from AddGameObject so invisible units are
			// never sent as creation packets here either.
			ForEachTileInSightWithout(
				*m_visibilityGrid,
				newTile->GetPosition(),
				oldTile->GetPosition(),
				[&object, &objects](VisibilityTile& tile)
				{
					for (auto* subscriber : tile.GetWatchers().getElements())
					{
						if (subscriber->GetGameUnit().GetGuid() == object.GetGuid())
						{
							continue;
						}

						if (object.IsUnit() && !object.AsUnit().CanBeSeenBy(subscriber->GetGameUnit()))
						{
							continue;
						}

						// Only spawn if the client does not already know about this object.
						// This guards against the case where the unit was spawned via
						// SpawnTileObjects (player logged in / moved tiles) and then the
						// creature moves to a tile that ForEachTileInSightWithout considers
						// "new" without having first sent a despawn.
						if (subscriber->IsObjectKnown(object.GetGuid()))
						{
							continue;
						}

						subscriber->NotifyObjectsSpawned(objects);
					}
				});

			// Add the object
			newTile->GetGameObjects().add(&object);
		}
	}

	void WorldInstance::SetEncounterState(uint32 slotId, uint32 state)
	{
		const auto it = m_encounterStates.find(slotId);
		const bool changed = (it == m_encounterStates.end()) || (it->second != state);

		m_encounterStates[slotId] = state;

		if (changed)
		{
			// Notify instance triggers that listen for encounter state changes.
			FireInstanceTriggerEvent(trigger_event::OnEncounterStateChanged, nullptr, { slotId, state });
		}
	}

	int64 WorldInstance::GetInstanceVariable(uint32 key) const
	{
		const auto it = m_instanceVariables.find(key);
		return (it != m_instanceVariables.end()) ? it->second : 0;
	}

	void WorldInstance::SetInstanceVariable(uint32 key, int64 value)
	{
		m_instanceVariables[key] = value;
	}

	uint32 WorldInstance::GetEncounterState(uint32 slotId) const
	{
		const auto it = m_encounterStates.find(slotId);
		return (it != m_encounterStates.end()) ? it->second : encounter_state::NotStarted;
	}

	void WorldInstance::FireInstanceTriggerEvent(trigger_event::Type eventType, GameUnitS* triggeringUnit)
	{
		FireInstanceTriggerEvent(eventType, triggeringUnit, {});
	}

	void WorldInstance::FireInstanceTriggerEvent(trigger_event::Type eventType, GameUnitS* triggeringUnit, const std::vector<uint32>& eventData)
	{
		if (!m_mapEntry)
		{
			return;
		}

		for (const uint32 triggerId : m_mapEntry->instance_triggers())
		{
			const auto* triggerEntry = m_project.triggers.getById(triggerId);
			if (!triggerEntry)
			{
				continue;
			}

			for (const auto& triggerEvent : triggerEntry->newevents())
			{
				if (triggerEvent.type() != eventType)
				{
					continue;
				}

				// For events with filter data (e.g. OnEncounterStateChanged), only fire when the
				// event's configured data matches the supplied data. A zero/absent filter matches all.
				bool dataMatches = true;
				for (int i = 0; i < triggerEvent.data_size(); ++i)
				{
					const uint32 filter = triggerEvent.data(i);
					if (filter == 0)
					{
						continue; // Wildcard for this slot
					}

					if (i >= static_cast<int>(eventData.size()) || eventData[i] != filter)
					{
						dataMatches = false;
						break;
					}
				}

				if (!dataMatches)
				{
					continue;
				}

				TriggerContext ctx(nullptr, triggeringUnit, this);
				m_triggerHandler.ExecuteTrigger(*triggerEntry, ctx);
				break;
			}
		}
	}

	void WorldInstance::StartInstanceTimers()
	{
		StopInstanceTimers();

		if (!m_mapEntry)
		{
			return;
		}

		for (const uint32 triggerId : m_mapEntry->instance_triggers())
		{
			const auto* triggerEntry = m_project.triggers.getById(triggerId);
			if (!triggerEntry)
			{
				continue;
			}

			bool hasTimerEvent = false;
			for (const auto& triggerEvent : triggerEntry->newevents())
			{
				if (triggerEvent.type() == trigger_event::OnTimer)
				{
					hasTimerEvent = true;
					break;
				}
			}

			if (!hasTimerEvent)
			{
				continue;
			}

			auto countdown = std::make_unique<Countdown>(m_universe.GetTimers());
			Countdown* countdownPtr = countdown.get();
			const proto::TriggerEntry* entryPtr = triggerEntry;

			countdown->ended.connect([this, countdownPtr, entryPtr]()
				{
					TriggerContext ctx(nullptr, nullptr, this);
					m_triggerHandler.ExecuteTrigger(*entryPtr, ctx);

					// Reschedule for the next interval.
					ScheduleInstanceTimer(*countdownPtr, *entryPtr);
				});

			ScheduleInstanceTimer(*countdownPtr, *triggerEntry);
			m_instanceTimers.push_back(std::move(countdown));
		}
	}

	void WorldInstance::StopInstanceTimers()
	{
		m_instanceTimers.clear();
	}

	void WorldInstance::ScheduleInstanceTimer(Countdown& countdown, const proto::TriggerEntry& entry)
	{
		uint32 minInterval = 0;
		uint32 maxInterval = 0;
		for (const auto& triggerEvent : entry.newevents())
		{
			if (triggerEvent.type() == trigger_event::OnTimer)
			{
				minInterval = triggerEvent.data_size() > 0 ? triggerEvent.data(0) : 0;
				maxInterval = triggerEvent.data_size() > 1 ? triggerEvent.data(1) : 0;
				break;
			}
		}

		if (minInterval == 0)
		{
			WLOG("Instance OnTimer trigger " << entry.id() << " has no/zero interval and will not fire");
			return;
		}

		uint32 interval = minInterval;
		if (maxInterval > minInterval)
		{
			std::uniform_int_distribution<uint32> dist(minInterval, maxInterval);
			interval = dist(randomGenerator);
		}

		countdown.SetEnd(GetAsyncTimeMs() + interval);
	}
}
