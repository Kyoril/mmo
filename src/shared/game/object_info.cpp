
#include "object_info.h"


#include "binary_io/reader.h"
#include "binary_io/writer.h"

namespace mmo
{
	io::Writer& operator<<(io::Writer& writer, const ObjectInfo& objectInfo)
	{
		writer
			<< io::write<uint64>(objectInfo.id)
			<< io::write<uint32>(objectInfo.type)
			<< io::write<uint32>(objectInfo.displayId)
			<< io::write_dynamic_range<uint8>(objectInfo.name)
			<< io::write_range(objectInfo.properties);

		return writer;
	}

	io::Reader& operator>>(io::Reader& reader, ObjectInfo& outObjectInfo)
	{
		reader
			>> io::read<uint64>(outObjectInfo.id)
			>> io::read<uint32>(outObjectInfo.type)
			>> io::read<uint32>(outObjectInfo.displayId)
			>> io::read_container<uint8>(outObjectInfo.name)
			>> io::read_range(outObjectInfo.properties);

		return reader;
	}
}
