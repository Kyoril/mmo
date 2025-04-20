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

		union
		{
			struct
			{
				uint32 startOpen;
				uint32 lockId;
				uint32 autoCloseTime;
			} door;

			struct
			{
				uint32 startOpen;
				uint32 lockId;
				uint32 autoCloseTime;
			} button;

			struct
			{
				uint32 lockId;
				uint32 questList;
				uint32 gossipMenuId;
			} questGiver;

			struct
			{
				uint32 lockId;
				uint32 lootId;
				uint32 chestRestockTime;
			} chest;

			uint32 data[16];
		};
	};

	io::Writer& operator<<(io::Writer& writer, const ObjectInfo& objectInfo);
	io::Reader& operator>>(io::Reader& reader, ObjectInfo& outObjectInfo);
}