// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "world_instance.h"
#include "world_instance_manager.h"
#include "regular_update.h"
#include "log/default_log_levels.h"

namespace mmo
{
	WorldInstance::WorldInstance(WorldInstanceManager& manager, MapId mapId)
		: m_manager(manager)
		, m_mapId(mapId)
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
