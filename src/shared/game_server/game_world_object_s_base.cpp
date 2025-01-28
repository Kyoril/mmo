
#include "game_world_object_s_base.h"

namespace mmo
{
	GameWorldObjectS_Base::GameWorldObjectS_Base(const proto::Project& project)
		: GameObjectS(project)
	{
	}

	GameWorldObjectS_Chest::GameWorldObjectS_Chest(const proto::Project& project)
		: GameWorldObjectS_Base(project)
	{
	}

	void GameWorldObjectS_Chest::Initialize()
	{
		GameWorldObjectS_Base::Initialize();


	}

	void GameWorldObjectS_Chest::PrepareFieldMap()
	{
		m_fields.Initialize(object_fields::WorldObjectFieldCount);
	}
}
