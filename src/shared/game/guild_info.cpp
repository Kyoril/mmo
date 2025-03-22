
#include "guild_info.h"

#include "binary_io/reader.h"
#include "binary_io/writer.h"

namespace mmo
{
	io::Writer& operator<<(io::Writer& writer, const GuildInfo& guildInfo)
	{
		writer
			<< io::write<uint64>(guildInfo.id)
			<< io::write_dynamic_range<uint8>(guildInfo.name);
		return writer;
	}
	io::Reader& operator>>(io::Reader& reader, GuildInfo& guildInfo)
	{
		reader
			>> io::read<uint64>(guildInfo.id)
			>> io::read_container<uint8>(guildInfo.name);
		return reader;
	}
}
