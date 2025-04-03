// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_instance.h"

#include "creature_spawner.h"
#include "each_tile_in_sight.h"
#include "game_creature_s.h"
#include "game_world_object_s_base.h"
#include "world_instance_manager.h"
#include "regular_update.h"
#include "tile_subscriber.h"
#include "universe.h"
#include "log/default_log_levels.h"
#include "visibility_grid.h"
#include "visibility_tile.h"
#include "base/utilities.h"
#include "binary_io/vector_sink.h"
#include "game_server/game_object_s.h"
#include "game_server/each_tile_in_region.h"
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
		// TODO
		return true;
	}

	NavMapData::NavMapData(const proto::MapEntry& mapEntry)
	{
		m_map = std::make_shared<nav::Map>(mapEntry.directory());

		// Load all map pages
		DLOG("Loading nav map pages...");
		m_map->LoadAllPages();
	}

	bool NavMapData::IsInLineOfSight(const Vector3& posA, const Vector3& posB)
	{
		// TODO
		return true;
	}

	bool NavMapData::CalculatePath(const Vector3& start, const Vector3& destination, std::vector<Vector3>& out_path) const
	{
		return m_map->FindPath(start, destination, out_path, true);
	}

	bool NavMapData::FindRandomPointAroundCircle(const Vector3& centerPosition, float radius, Vector3& randomPoint) const
	{
		return m_map->FindRandomPointAroundCircle(centerPosition, radius, randomPoint);
	}

	WorldInstance::WorldInstance(WorldInstanceManager& manager, Universe& universe, IdGenerator<uint64>& objectIdGenerator, const proto::Project& project, const MapId mapId, std::unique_ptr<VisibilityGrid> visibilityGrid, std::unique_ptr<UnitFinder> unitFinder, ITriggerHandler& triggerHandler)
		: m_universe(universe)
		, m_objectIdGenerator(objectIdGenerator)
		, m_manager(manager)
		, m_mapId(mapId)
		, m_project(project)
		, m_visibilityGrid(std::move(visibilityGrid))
		, m_unitFinder(std::move(unitFinder))
		, m_triggerHandler(triggerHandler)
	{
		uuids::uuid_system_generator generator;
		m_id = generator();

		m_mapEntry = m_project.maps.getById(m_mapId);
		if (!m_mapEntry)
		{
			ELOG("Failed to load map data for map id " << m_mapId << ": Map not found!");
			return;
		}

		m_mapData = std::make_unique<NavMapData>(*m_mapEntry);

		// Add object spawners
		for (int i = 0; i < m_mapEntry->objectspawns_size(); ++i)
		{
			// Create a new spawner
			const auto& spawn = m_mapEntry->objectspawns(i);

			const auto* objectEntry = m_project.objects.getById(spawn.objectentry());
			ASSERT(objectEntry);

			std::unique_ptr<WorldObjectSpawner> spawner(new WorldObjectSpawner(
				*this,
				*objectEntry,
				spawn.maxcount(),
				spawn.respawndelay(),
				Vector3(spawn.location().positionx(), spawn.location().positiony(), spawn.location().positionz()),
				{ spawn.location().rotationw(), spawn.location().rotationx(), spawn.location().rotationy(), spawn.location().rotationz() },
				spawn.radius(),
				spawn.animprogress(),
				spawn.state()));
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
			ASSERT(unitEntry);

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

		for (const auto& object : m_objectUpdates)
		{
			UpdateObject(*object);
		}
		
		m_updating = false;
		m_objectUpdates = m_queuedObjectUpdates;
	}

	void WorldInstance::AddGameObject(GameObjectS& added)
	{
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
		    std::vector objects { &added };
		    for (auto *subscriber : tile.GetWatchers())
			{
				if (subscriber->GetGameUnit().GetGuid() == added.GetGuid())
				{
					continue;
				}

				DLOG("Notifying subscriber " << log_hex_digit(subscriber->GetGameUnit().GetGuid()) << " about spawn of character " << log_hex_digit(added.GetGuid()));
		    	subscriber->NotifyObjectsSpawned(objects);
			}
		});

		if (GameUnitS* addedUnit = dynamic_cast<GameUnitS*>(&added))
		{
			m_unitFinder->AddUnit(*addedUnit);
		}

		if (added.IsUnit())
		{
			added.AsUnit().unitTrigger.connect([this](const proto::TriggerEntry& trigger, GameUnitS& owner, GameUnitS* triggeringUnit) {
				m_triggerHandler.ExecuteTrigger(trigger, TriggerContext(&owner, triggeringUnit), 0);
				});
		}
	}

	void WorldInstance::RemoveGameObject(GameObjectS& remove)
	{
		if (GameUnitS* removedUnit = dynamic_cast<GameUnitS*>(&remove))
		{
			m_unitFinder->RemoveUnit(*removedUnit);
		}

		const auto it = m_objectsByGuid.find(remove.GetGuid());
		if (it == m_objectsByGuid.end())
		{
			ELOG("Could not find object!");
			return;
		}

		DLOG("Removing object " << log_hex_digit(remove.GetGuid()) << " from world instance ...");
		m_objectsByGuid.erase(it);

		// Clear update
		if (m_queuedObjectUpdates.contains(&remove))
		{
			m_queuedObjectUpdates.erase(&remove);
		}
		if (m_objectUpdates.contains(&remove))
		{
			m_objectUpdates.erase(&remove);
		}

		// No need for visibility updates for item objects
		if (!remove.IsItem() && !remove.IsContainer())
		{
			TileIndex2D gridIndex;
			if (!m_visibilityGrid->GetTilePosition(remove.GetPosition(), gridIndex[0], gridIndex[1]))
			{
				ELOG("Could not resolve grid location!");
				return;
			}

			auto* tile = m_visibilityGrid->GetTile(gridIndex);
			if (!tile)
			{
				ELOG("Could not find tile!");
				return;
			}

			tile->GetGameObjects().remove(&remove);

			remove.SetWorldInstance(nullptr);
			remove.despawned(remove);

			ForEachTileInSight(
				*m_visibilityGrid,
				tile->GetPosition(),
				[&remove](VisibilityTile& tile)
				{
					std::vector objects{ &remove };
					for (auto* subscriber : tile.GetWatchers())
					{
						subscriber->NotifyObjectsDespawned(objects);
					}
				});
		}

		if (remove.destroy)
		{
			remove.destroy(remove);
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

		spawned->Initialize();
		spawned->Set<uint64>(object_fields::Guid, CreateEntryGUID(m_objectIdGenerator.GenerateId(), entry.id(), GuidType::Unit));
		spawned->ApplyMovementInfo(
			{ movement_flags::None, GetAsyncTimeMs(), position, Radian(o), Radian(0), 0, 0.0f, 0.0, 0.0f, 0.0f });

		// TODO: This might be bad because we aren't technically really spawned in this world yet! We do this only so that passives can be cast!
		spawned->SetWorldInstance(this);
		spawned->SetEntry(entry);
		
		return spawned;
	}

	std::shared_ptr<GameWorldObjectS_Base> WorldInstance::SpawnWorldObject(const proto::ObjectEntry& entry, const Vector3& position)
	{
		// Create the object
		auto spawned = std::make_shared<GameWorldObjectS_Chest>(m_project, entry);

		spawned->Initialize();
		spawned->Set<uint64>(object_fields::Guid, CreateEntryGUID(m_objectIdGenerator.GenerateId(), entry.id(), GuidType::Object));
		DLOG("Spawned world object: " << log_hex_digit(spawned->GetGuid()));
		spawned->ApplyMovementInfo(
			{ movement_flags::None, GetAsyncTimeMs(), position, Radian(0.0f), Radian(0), 0, 0.0f, 0.0, 0.0f, 0.0f });
		spawned->SetWorldInstance(this);

		return std::static_pointer_cast<GameWorldObjectS_Base>(spawned);
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
			[&objects](TileSubscriber& subscriber)
			{
				auto& character = subscriber.GetGameUnit();
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

			// Send despawn packets
			ForEachTileInSightWithout(
				*m_visibilityGrid,
				oldTile->GetPosition(),
				newTile->GetPosition(),
				[&object, &objects](VisibilityTile& tile)
				{
					const uint64 guid = object.GetGuid();

					// Despawn this object for all subscribers
					for (auto* subscriber : tile.GetWatchers().getElements())
					{
						// This is the subscribers own character - despawn all old objects and skip him
						if (subscriber->GetGameUnit().GetGuid() == guid)
						{
							continue;
						}

						subscriber->NotifyObjectsDespawned(objects);
					}
				});

			// Notify watchers about the pending tile change
			object.tileChangePending(*oldTile, *newTile);

			// Send spawn packets
			ForEachTileInSightWithout(
				*m_visibilityGrid,
				newTile->GetPosition(),
				oldTile->GetPosition(),
				[&object, &objects](VisibilityTile& tile)
				{
					// Spawn this new object for all watchers of the new tile
					for (auto* subscriber : tile.GetWatchers().getElements())
					{
						// TODO: Spawn conditions for watcher

						// This is the subscribers own character - send all new objects to this subscriber
						// and then skip him
						if (subscriber->GetGameUnit().GetGuid() == object.GetGuid())
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
}
