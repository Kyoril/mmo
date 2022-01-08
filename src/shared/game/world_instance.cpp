// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "world_instance.h"
#include "world_instance_manager.h"
#include "regular_update.h"
#include "log/default_log_levels.h"
#include "visibility_grid.h"
#include "visibility_tile.h"
#include "game/game_object.h"

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

	void WorldInstance::AddGameObject(GameObject& added)
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

		// Spawn
	}

	void WorldInstance::RemoveGameObject(GameObject& remove)
	{

	}

	void WorldInstance::AddObjectUpdate(GameObject& object)
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

	void WorldInstance::RemoveObjectUpdate(GameObject& object)
	{

	}
}
