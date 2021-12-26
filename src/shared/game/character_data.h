#pragma once

#include "base/typedefs.h"
#include "math/radian.h"
#include "math/vector3.h"
#include "game.h"

namespace mmo
{
	struct CharacterData
	{
		explicit CharacterData(const ObjectId characterId)
			: characterId(characterId)
			, mapId(0)
		{
		}
		explicit CharacterData(const ObjectId characterId, const String& name, const MapId mapId, const InstanceId& instanceId, const Vector3& position, const Radian& facing)
			: characterId(characterId)
			, name(name)
			, mapId(mapId)
			, instanceId(instanceId)
			, position(position)
			, facing(facing)
		{
		}

		ObjectId characterId;
		String name;
		MapId mapId;
		InstanceId instanceId;
		Vector3 position;
		Radian facing;
	};
}
