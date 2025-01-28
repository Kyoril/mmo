
#include "game_world_object_c_base.h"

namespace mmo
{
	GameWorldObjectC_Base::GameWorldObjectC_Base(Scene& scene, const proto_client::Project& project)
		: GameObjectC(scene, project)
	{
	}

	GameWorldObjectC_Chest::GameWorldObjectC_Chest(Scene& scene, const proto_client::Project& project)
		: GameWorldObjectC_Base(scene, project)
	{
	}
}
