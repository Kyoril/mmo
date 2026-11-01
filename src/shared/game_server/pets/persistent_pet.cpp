// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "persistent_pet.h"

namespace mmo
{
	io::Writer& operator<<(io::Writer& writer, const PersistentPetData& data)
	{
		return writer
			<< io::write<uint64>(data.dbId)
			<< io::write<uint32>(data.classId)
			<< io::write<uint32>(data.petEntry)
			<< io::write<uint32>(data.unitEntry)
			<< io::write_dynamic_range<uint8>(data.name)
			<< io::write<uint32>(data.level)
			<< io::write<uint32>(data.xp)
			<< io::write<uint32>(data.health)
			<< io::write<uint32>(data.power)
			<< io::write<uint8>(data.isDead ? 1 : 0)
			<< io::write<uint8>(data.wasSummoned ? 1 : 0)
			<< io::write_dynamic_range<uint16>(data.spells);
	}

	io::Reader& operator>>(io::Reader& reader, PersistentPetData& data)
	{
		uint8 isDead = 0;
		uint8 wasSummoned = 0;
		reader
			>> io::read<uint64>(data.dbId)
			>> io::read<uint32>(data.classId)
			>> io::read<uint32>(data.petEntry)
			>> io::read<uint32>(data.unitEntry)
			>> io::read_container<uint8>(data.name)
			>> io::read<uint32>(data.level)
			>> io::read<uint32>(data.xp)
			>> io::read<uint32>(data.health)
			>> io::read<uint32>(data.power)
			>> io::read<uint8>(isDead)
			>> io::read<uint8>(wasSummoned)
			>> io::read_container<uint16>(data.spells);

		data.isDead = isDead != 0;
		data.wasSummoned = wasSummoned != 0;
		return reader;
	}
}
