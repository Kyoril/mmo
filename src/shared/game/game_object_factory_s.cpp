
#include "game_object_factory_s.h"
#include "game_player_s.h"
#include "game_unit_s.h"

namespace mmo
{
	std::shared_ptr<GameObjectS> GameObjectFactoryS::CreateGameObject(ObjectGuid guid, ObjectTypeId typeId)
	{
		switch(typeId)
		{
		case ObjectTypeId::Object:
			return std::make_unique<GameObjectS>(guid);
		case ObjectTypeId::Unit:
			return std::make_unique<GameUnitS>(guid);
		case ObjectTypeId::Player:
			return std::make_unique<GamePlayerS>(guid);
		default:
			return nullptr;
		}
	}
}
