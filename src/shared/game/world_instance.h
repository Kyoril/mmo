// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <unordered_set>

#include "game/game.h"
#include "visibility_grid.h"

namespace mmo
{
	class GameObject;
	class WorldInstanceManager;
	class RegularUpdate;
	class VisibilityGrid;
	
	/// Represents a single world instance at the world server.
	class WorldInstance
	{
	public:
		explicit WorldInstance(WorldInstanceManager& manager, MapId mapId, std::unique_ptr<VisibilityGrid> visibilityGrid);
	
	public:
		
		/// Called to update the world instance once every tick.
		void Update(const RegularUpdate& update);
		
		/// Gets the id of this world instance.
		[[nodiscard]] InstanceId GetId() const noexcept { return m_id; }
		
		/// Gets the map id of this world instance.
		[[nodiscard]] MapId GetMapId() const noexcept { return m_mapId; }
		
		/// Adds a game object to this world instance.
		void AddGameObject(GameObject &added);

		/// Removes a specific game object from this world.
		void RemoveGameObject(GameObject &remove);

		// Not thread safe
		void AddObjectUpdate(GameObject& object);
		
		// Not thread safe
		void RemoveObjectUpdate(GameObject& object);

		VisibilityGrid& GetGrid() const;


	private:
		WorldInstanceManager& m_manager;
		InstanceId m_id;
		MapId m_mapId;
		volatile bool m_updating { false };
		std::unordered_set<GameObject*> m_objectUpdates;
		std::unordered_set<GameObject*> m_queuedObjectUpdates;
		std::unique_ptr<VisibilityGrid> m_visibilityGrid;
		
		typedef std::unordered_map<uint64, GameObject*> GameObjectsByGuid;
		GameObjectsByGuid m_objectsByGuid;
	};
}
