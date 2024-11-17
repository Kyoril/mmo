
#include "quest_info.h"

#include "binary_io/reader.h"
#include "binary_io/writer.h"

namespace mmo
{
	io::Writer& operator<<(io::Writer& writer, const QuestInfo& itemInfo)
	{
		writer << io::write<uint64>(itemInfo.id)
			<< io::write_dynamic_range<uint8>(itemInfo.title)
			<< io::write_dynamic_range<uint8>(itemInfo.summary)
			<< io::write_dynamic_range<uint8>(itemInfo.description);

		return writer;
	}

	io::Reader& operator>>(io::Reader& reader, QuestInfo& outItemInfo)
	{
		reader >> io::read<uint64>(outItemInfo.id)
			>> io::read_container<uint8>(outItemInfo.title)
			>> io::read_container<uint8>(outItemInfo.summary)
			>> io::read_container<uint8>(outItemInfo.description);

		return reader;
	}
}
