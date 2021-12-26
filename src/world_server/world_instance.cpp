
#include "world_instance.h"
#include "world_instance_manager.h"
#include "regular_update.h"

namespace mmo
{
	WorldInstance::WorldInstance(WorldInstanceManager& manager)
		: m_manager(manager)
	{
		uuids::uuid_system_generator generator;
		m_id = generator();
	}

	void WorldInstance::Update(const RegularUpdate& update)
	{
		// TODO: Update all game objects that need updates
	}
}
