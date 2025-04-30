
#include "world_entity_loader.h"

#include <random>

#include "base/chunk_writer.h"
#include "log/default_log_levels.h"

namespace mmo
{
	static ChunkMagic WorldEntityVersionChunk = MakeChunkMagic('WVER');
	static ChunkMagic WorldEntityMesh = MakeChunkMagic('WMSH');

	uint64 GenerateUniqueId()
	{
		// Get current time in milliseconds since epoch
		const auto now = std::chrono::system_clock::now();
		const uint64 timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

		// Mix with random bits
		std::random_device rd;
		std::mt19937_64 gen(rd());
		std::uniform_int_distribution<uint64> dis;
		const uint64 random = dis(gen) & 0x0000FFFFFFFFFFFF;

		// Format: 16 bits from timestamp + 48 bits random
		return (timestamp & 0xFFFF) << 48 | random;
	}

	WorldEntityLoader::WorldEntityLoader()
	{
		m_ignoreUnhandledChunks = true;

		AddChunkHandler(*WorldEntityVersionChunk, true, *this, &WorldEntityLoader::OnVersionChunk);
	}

	bool WorldEntityLoader::OnVersionChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		RemoveChunkHandler(*WorldEntityVersionChunk);
		if (!(reader >> io::read<uint32>(m_version)))
		{
			ELOG("Failed to read world entity version");
			return false;
		}

		if (m_version != 0x00001)
		{
			ELOG("Unsupported world entity version " << m_version);
			return false;
		}

		AddChunkHandler(*WorldEntityMesh, true, *this, &WorldEntityLoader::OnEntityMeshChunk);
		return reader;
	}

	bool WorldEntityLoader::OnEntityMeshChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		ASSERT(chunkHeader == *WorldEntityMesh);

		if (!(reader
			>> io::read<uint64>(m_entity.uniqueId)
			>> io::read_container<uint16>(m_entity.meshName)
			>> io::read<float>(m_entity.position.x)
			>> io::read<float>(m_entity.position.y)
			>> io::read<float>(m_entity.position.z)
			>> io::read<float>(m_entity.rotation.w)
			>> io::read<float>(m_entity.rotation.x)
			>> io::read<float>(m_entity.rotation.y)
			>> io::read<float>(m_entity.rotation.z)
			>> io::read<float>(m_entity.scale.x)
			>> io::read<float>(m_entity.scale.y)
			>> io::read<float>(m_entity.scale.z)
		))
		{
			ELOG("Failed to read map entity chunk content, unexpected end of file!");
			return false;
		}

		uint8 numMaterialOverrides;
		if (!(reader >> io::read<uint8>(numMaterialOverrides)))
		{
			ELOG("Failed to read material override count for map entity chunk, unexpected end of file!");
			return false;
		}

		m_entity.materialOverrides.resize(numMaterialOverrides);
		for (uint8 i = 0; i < numMaterialOverrides; ++i)
		{
			if (!(reader >> io::read<uint8>(m_entity.materialOverrides[i].materialIndex) >> io::read_container<uint16>(m_entity.materialOverrides[i].materialName)))
			{
				ELOG("Failed to read material override for map entity chunk, unexpected end of file!");
				return false;
			}
		}

		if (m_entity.uniqueId == 0)
		{
			m_entity.uniqueId = GenerateUniqueId();
		}

		return reader;
	}
}
