
#include "map.h"

#include "assets/asset_registry.h"
#include "base/macros.h"

#include "binary_io/stream_source.h"
#include "binary_io/reader.h"

namespace mmo
{
	Map::Map()
	{
		AddChunkHandler('MVER', true, [this](io::Reader& reader, uint32 chunkHeader, uint32 size) -> bool
		{
			reader >> io::read<uint32>(m_version);
			ASSERT(m_version == 0x0001);
			return reader;
		});
	}

	void Map::Load(const std::filesystem::path& filename)
	{
		const std::unique_ptr<std::istream> file = AssetRegistry::OpenFile(filename.string());
		ASSERT(file);

		io::StreamSource source(*file);
		io::Reader reader(source);
		if (!this->Read(reader))
		{
			ASSERT(!"Failed to read map file");
		}
	}
}
