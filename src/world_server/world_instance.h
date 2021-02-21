
#pragma once

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

	private:
		WorldInstanceManager& m_manager;
	};
}