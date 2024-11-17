#pragma once

#include "base/typedefs.h"

namespace io
{
	class Reader;
	class Writer;
}

namespace mmo
{
	struct CreatureInfo
	{
		uint64 id;
		String name;
		String subname;
	};

	io::Writer& operator<<(io::Writer& writer, const CreatureInfo& info);
	io::Reader& operator>>(io::Reader& reader, CreatureInfo& outInfo);
}
