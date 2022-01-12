
#include "game_object_factory.h"
#include "game_player.h"
#include "game_unit.h"

namespace mmo
{
	std::shared_ptr<GameObjectS> GameObjectFactory::CreateGameObject(ObjectGuid guid, ObjectTypeId typeId)
	{
		switch(typeId)
		{
		case ObjectTypeId::Object:
			return std::make_unique<GameObjectS>(guid);
		case ObjectTypeId::Unit:
			return std::make_unique<GameUnit>(guid);
		case ObjectTypeId::Player:
			return std::make_unique<GamePlayer>(guid);
		default:
			return nullptr;
		}
	}
}
