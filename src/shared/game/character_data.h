// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/radian.h"
#include "math/vector3.h"
#include "game.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"
#include "game_server/inventory.h"

namespace mmo
{
	struct CharacterData
	{
		explicit CharacterData()
			: CharacterData(0, "", 0, InstanceId(), Vector3::Zero, Radian(0.0f), {}, 0, 0, 0, 1, 0, 20, 0, 0, 0, 0, Vector3::Zero, Radian(0.0f))
		{
		}

		explicit CharacterData(const ObjectId characterId, String name, const MapId mapId, const InstanceId& instanceId, const Vector3& position, const Radian& facing, const std::vector<uint32>& spellIds,
			uint32 classId, uint32 raceId, uint8 gender, uint8 level, uint32 xp, uint32 hp, uint32 mana, uint32 rage, uint32 energy, uint32 bindMap, const Vector3& bindPosition, const Radian& bindFacing)
			: characterId(characterId)
			, name(std::move(name))
			, mapId(mapId)
			, instanceId(instanceId)
			, position(position)
			, facing(facing)
			, classId(classId)
			, raceId(raceId)
			, gender(gender)
			, level(level)
			, xp(xp)
			, hp(hp)
			, mana(mana)
			, rage(rage)
			, energy(energy)
			, spellIds(spellIds)
			, bindMap(bindMap)
			, bindPosition(bindPosition)
			, bindFacing(bindFacing)
		{
		}

		ObjectId characterId;
		String name;
		MapId mapId;
		InstanceId instanceId;
		Vector3 position;
		Radian facing;
		uint32 classId;
		uint32 raceId;
		uint8 gender;
		uint8 level;
		uint32 xp;
		uint32 hp;
		uint32 mana;
		uint32 rage;
		uint32 energy;
		uint32 money;
		std::vector<uint32> spellIds;
		std::vector<ItemData> items;

		uint32 bindMap;
		Vector3 bindPosition;
		Radian bindFacing;
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
			>> io::read_container<uint16>(data.spellIds)
			>> io::read<uint32>(data.classId)
			>> io::read<uint32>(data.raceId)
			>> io::read<uint32>(data.gender)
			>> io::read<uint8>(data.level)
			>> io::read<uint32>(data.xp)
			>> io::read<uint32>(data.hp)
			>> io::read<uint32>(data.mana)
			>> io::read<uint32>(data.rage)
			>> io::read<uint32>(data.energy)
			>> io::read<uint32>(data.money)
			>> io::read_container<uint16>(data.items)
			>> io::read<uint32>(data.bindMap)
			>> io::read<float>(data.bindPosition.x)
			>> io::read<float>(data.bindPosition.y)
			>> io::read<float>(data.bindPosition.z)
			>> data.bindFacing
		;
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
			<< io::write_dynamic_range<uint16>(data.spellIds)
			<< io::write<uint32>(data.classId)
			<< io::write<uint32>(data.raceId)
			<< io::write<uint32>(data.gender)
			<< io::write<uint8>(data.level)
			<< io::write<uint32>(data.xp)
			<< io::write<uint32>(data.hp)
			<< io::write<uint32>(data.mana)
			<< io::write<uint32>(data.rage)
			<< io::write<uint32>(data.energy)
			<< io::write<uint32>(data.money)
			<< io::write_dynamic_range<uint16>(data.items)
			<< io::write<uint32>(data.bindMap)
			<< io::write<float>(data.bindPosition.x)
			<< io::write<float>(data.bindPosition.y)
			<< io::write<float>(data.bindPosition.z)
			<< data.facing
			;
	}
}
