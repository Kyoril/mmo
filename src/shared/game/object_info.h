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
			/// Door data layout (matches server-side ObjectEntry.data usage).
			struct
			{
				/// Lock type id required to open the door (0 = no lock).
				uint32 lockId;
				/// Lock type id applied after a successful unlock (0 = unchanged).
				uint32 postUnlockLockId;
				/// Time in milliseconds after opening before the door closes itself (0 = never).
				uint32 autoCloseTime;
			} door;

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