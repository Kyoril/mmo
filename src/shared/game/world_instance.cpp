// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "world_instance.h"
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
		
		added.spawned(*this);
		
		ForEachTileInSight(
		    *m_visibilityGrid,
		    tile.GetPosition(),
		    [&added](VisibilityTile & tile)
		{
		    std::vector objects { &added };
		    for (const auto *subscriber : tile.GetWatchers())
			{
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

	}

	VisibilityGrid& WorldInstance::GetGrid() const
	{
		ASSERT(m_visibilityGrid);
		return *m_visibilityGrid;
	}
}
