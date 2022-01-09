#pragma once

#include "game_object.h"

#include <memory>

namespace mmo
{
	class GameObjectFactory final
	{
	public:
		/// @brief Create a new game object for the specified type.
		/// @param guid The guid of the object.
		/// @param typeId The type id.
		/// @return A new game object.
		std::shared_ptr<GameObject> CreateGameObject(ObjectGuid guid, ObjectTypeId typeId);
	};
}
