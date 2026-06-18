// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "character_view.h"

#include "binary_io/reader.h"
#include "binary_io/writer.h"

namespace mmo
{
	io::Writer& operator<<(io::Writer& writer, const CharacterView& characterView)
	{
		writer
			<< io::write<uint64>(characterView.m_guid)
			<< io::write_dynamic_range<uint8>(characterView.m_name)
			<< io::write<uint8>(characterView.m_level)
			<< io::write<uint32>(characterView.m_mapId)
			<< io::write<uint32>(characterView.m_zoneId)
			<< io::write<uint32>(characterView.m_raceId)
			<< io::write<uint32>(characterView.m_classId)
			<< io::write<uint8>(characterView.m_gender)
			<< io::write<uint8>(characterView.m_dead ? 1 : 0)
			<< io::write<uint32>(characterView.m_displayId)
			<< characterView.m_configuration;

		for (uint8 i = 0; i < player_equipment_slots::Count_; ++i)
		{
			writer << io::write<uint32>(characterView.m_equipmentDisplayIds[i]);
		}

		return writer;
	}

	io::Reader& operator>>(io::Reader& reader, CharacterView& outCharacterView)
	{
		reader
			>> io::read<uint64>(outCharacterView.m_guid)
			>> io::read_container<uint8>(outCharacterView.m_name)
			>> io::read<uint8>(outCharacterView.m_level)
			>> io::read<uint32>(outCharacterView.m_mapId)
			>> io::read<uint32>(outCharacterView.m_zoneId)
			>> io::read<uint32>(outCharacterView.m_raceId)
			>> io::read<uint32>(outCharacterView.m_classId)
			>> io::read<uint8>(outCharacterView.m_gender)
			>> io::read<uint8>(outCharacterView.m_dead)
			>> io::read<uint32>(outCharacterView.m_displayId)
			>> outCharacterView.m_configuration;

		for (uint8 i = 0; i < player_equipment_slots::Count_; ++i)
		{
			reader >> io::read<uint32>(outCharacterView.m_equipmentDisplayIds[i]);
		}

		return reader;
	}
}
