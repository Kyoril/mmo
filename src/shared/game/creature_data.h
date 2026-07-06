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
		/// Sound file played when this creature detects a stealthed player. Empty = default.
		String stealthAlertSound;
	};

	io::Writer& operator<<(io::Writer& writer, const CreatureInfo& info);
	io::Reader& operator>>(io::Reader& reader, CreatureInfo& outInfo);
}
