// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/radian.h"
#include "math/vector3.h"
#include "game.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"

namespace mmo
{
	struct CharacterData
	{
		explicit CharacterData()
			: CharacterData(0, "", 0, InstanceId(), Vector3::Zero, Radian(0.0f), {})
		{
		}

		explicit CharacterData(const ObjectId characterId, String name, const MapId mapId, const InstanceId& instanceId, const Vector3& position, const Radian& facing, const std::vector<uint32>& spellIds)
			: characterId(characterId)
			, name(std::move(name))
			, mapId(mapId)
			, instanceId(instanceId)
			, position(position)
			, facing(facing)
			, spellIds(spellIds)
		{
		}

		ObjectId characterId;
		String name;
		MapId mapId;
		InstanceId instanceId;
		Vector3 position;
		Radian facing;

		std::vector<uint32> spellIds;
	};

	inline io::Reader& operator>>(io::Reader& reader, CharacterData& data)
	{
		return reader
			>> io::read_packed_guid(data.characterId)
			>> io::read<MapId>(data.mapId)
			>> io::read_container<uint8>(data.name)
			>> io::read<float>(data.position.x)
			>> io::read<float>(data.position.y)
			>> io::read<float>(data.position.z)
			>> data.facing
			>> io::read_container<uint16>(data.spellIds);
	}
	
	inline io::Writer& operator<<(io::Writer& reader, const CharacterData& data)
	{
		return reader
			<< io::write_packed_guid(data.characterId)
			<< io::write<MapId>(data.mapId)
			<< io::write_dynamic_range<uint8>(data.name)
			<< io::write<float>(data.position.x)
			<< io::write<float>(data.position.y)
			<< io::write<float>(data.position.z)
			<< data.facing
			<< io::write_dynamic_range<uint16>(data.spellIds);
	}
}
