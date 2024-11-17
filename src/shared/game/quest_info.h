#pragma once

#include "base/typedefs.h"

namespace io
{
	class Reader;
	class Writer;
}

namespace mmo
{

	struct QuestInfo
	{
		uint64 id;
		String title;
		String summary;
		String description;
	};

	io::Writer& operator<<(io::Writer& writer, const QuestInfo& itemInfo);
	io::Reader& operator>>(io::Reader& reader, QuestInfo& outItemInfo);
}
