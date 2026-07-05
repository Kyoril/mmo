
#include "creature_data.h"

#include "binary_io/reader.h"
#include "binary_io/writer.h"

namespace mmo
{
	io::Writer& operator<<(io::Writer& writer, const CreatureInfo& info)
	{
		writer << io::write<uint64>(info.id)
			<< io::write_dynamic_range<uint8>(info.name)
			<< io::write_dynamic_range<uint8>(info.subname)
			<< io::write_dynamic_range<uint8>(info.stealthAlertSound);

		return writer;
	}

	io::Reader& operator>>(io::Reader& reader, CreatureInfo& outInfo)
	{
		reader >> io::read<uint64>(outInfo.id)
			>> io::read_container<uint8>(outInfo.name)
			>> io::read_container<uint8>(outInfo.subname)
			>> io::read_container<uint8>(outInfo.stealthAlertSound);

		return reader;
	}
}