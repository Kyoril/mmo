// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "world_instance.h"

#include "each_tile_in_sight.h"
#include "game_player.h"
#include "world_instance_manager.h"
#include "regular_update.h"
#include "tile_subscriber.h"
#include "log/default_log_levels.h"
#include "visibility_grid.h"
#include "visibility_tile.h"
#include "base/utilities.h"
#include "binary_io/vector_sink.h"
#include "game/game_object_s.h"
#include "game/each_tile_in_region.h"

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

	WorldInstance::WorldInstance(WorldInstanceManager& manager, MapId mapId, std::unique_ptr<VisibilityGrid> visibilityGrid)
		: m_manager(manager)
		, m_mapId(mapId)
		, m_visibilityGrid(std::move(visibilityGrid))
	{
		uuids::uuid_system_generator generator;
		m_id = generator();
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
		    for (const auto *subscriber : tile.GetWatchers())
			{
				if (subscriber->GetGameUnit().GetGuid() == added.GetGuid())
				{
					continue;
				}

				DLOG("Notifying subscriber " << log_hex_digit(subscriber->GetGameUnit().GetGuid()) << " about spawn of character " << log_hex_digit(added.GetGuid()));
		    	subscriber->NotifyObjectsSpawned(objects);
			}
		});
	}

	void WorldInstance::RemoveGameObject(GameObjectS& remove)
	{
		const auto it = m_objectsByGuid.find(remove.GetGuid());
		if (it == m_objectsByGuid.end())
		{
			ELOG("Could not find object!");
			return;
		}
		
		TileIndex2D gridIndex;
		if (!m_visibilityGrid->GetTilePosition(remove.GetPosition(), gridIndex[0], gridIndex[1]))
		{
			ELOG("Could not resolve grid location!");
			return;
		}
		
		auto *tile = m_visibilityGrid->GetTile(gridIndex);
		if (!tile)
		{
			ELOG("Could not find tile!");
			return;
		}

		DLOG("Removing object " << log_hex_digit(remove.GetGuid()) << " from world instance ...");

		tile->GetGameObjects().remove(&remove);
		remove.SetWorldInstance(nullptr);
		remove.despawned(remove);

		ForEachTileInSight(
		    *m_visibilityGrid,
		    tile->GetPosition(),
		    [&remove](VisibilityTile & tile)
		{
		    std::vector objects { &remove };
		    for (const auto *subscriber : tile.GetWatchers())
			{
				subscriber->NotifyObjectsDespawned(objects);
			}
		});
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

	VisibilityGrid& WorldInstance::GetGrid() const
	{
		ASSERT(m_visibilityGrid);
		return *m_visibilityGrid;
	}

	void WorldInstance::NotifyObjectMoved(GameObjectS& object, const MovementInfo& previousMovementInfo,
		const MovementInfo& newMovementInfo)
	{
		OnObjectMoved(object, previousMovementInfo);
	}

	void WorldInstance::UpdateObject(GameObjectS& object)
	{
		const std::vector objects{ &object };

		// Send updates to all subscribers in sight
		const TileIndex2D center = GetObjectTile(object, *m_visibilityGrid);
		ForEachSubscriberInSight(
			*m_visibilityGrid,
			center,
			[&object, &objects](TileSubscriber& subscriber)
			{
				auto& character = subscriber.GetGameUnit();

				DLOG("Notifying subscriber " << log_hex_digit(subscriber.GetGameUnit().GetGuid()) << " about spawn of character " << log_hex_digit(object.GetGuid()));
				subscriber.NotifyObjectsSpawned(objects);
			});

		object.ClearFieldChanges();
	}

	void WorldInstance::OnObjectMoved(GameObjectS& object, const MovementInfo& oldMovementInfo)
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
					for (const auto* subscriber : tile.GetWatchers().getElements())
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
