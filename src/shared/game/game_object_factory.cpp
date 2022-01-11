
#include "game_object_factory.h"
#include "game_player.h"
#include "game_unit.h"

namespace mmo
{
	std::shared_ptr<GameObject> GameObjectFactory::CreateGameObject(ObjectGuid guid, ObjectTypeId typeId)
	{
		switch(typeId)
		{
		case ObjectTypeId::Object:
			return std::make_unique<GameObject>(guid);
		case ObjectTypeId::Unit:
			return std::make_unique<GameUnit>(guid);
		case ObjectTypeId::Player:
			return std::make_unique<GamePlayer>(guid);
		default:
			return nullptr;
		}
	}
}
