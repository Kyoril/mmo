#pragma once

#include <array>

#include "base/typedefs.h"

namespace io
{
	class Reader;
	class Writer;
}

namespace mmo
{
	struct ObjectInfo
	{
		uint64 id;
		uint32 type;
		uint32 displayId;
		String name;
		std::array<int32, 10> properties;
	};

	io::Writer& operator<<(io::Writer& writer, const ObjectInfo& objectInfo);
	io::Reader& operator>>(io::Reader& reader, ObjectInfo& outObjectInfo);
}