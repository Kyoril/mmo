
#pragma once

#include "base/typedefs.h"
#include "game/game.h"

namespace mmo
{
	class WorldInstanceManager;
	class RegularUpdate;
	
	/// Represents a single world instance at the world server.
	class WorldInstance
	{
	public:
		explicit WorldInstance(WorldInstanceManager& manager);
	
	public:
		
		/// Called to update the world instance once every tick.
		void Update(const RegularUpdate& update);

		/// Gets the id of this world instance.
		[[nodiscard]] InstanceId GetId() const noexcept { return m_id; }
		
		/// Gets the map id of this world instance.
		[[nodiscard]] MapId GetMapId() const noexcept { return m_mapId; }

	private:
		WorldInstanceManager& m_manager;
		InstanceId m_id;
		MapId m_mapId;
	};
}
