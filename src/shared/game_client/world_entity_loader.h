#pragma once

#include "base/chunk_reader.h"
#include "math/vector3.h"
#include "math/quaternion.h"

namespace mmo
{
	uint64 GenerateUniqueId();

	class WorldEntityLoader final : public ChunkReader
	{
	public:
		struct MaterialOverride
		{
			uint8 materialIndex;
			String materialName;
		};

		struct MapEntity
		{
			String meshName;
			Vector3 position;
			Quaternion rotation;
			Vector3 scale;
			uint64 uniqueId;
			std::vector<MaterialOverride> materialOverrides;
		};

	public:
		explicit WorldEntityLoader();

		const MapEntity& GetEntity() const
		{
			return m_entity;
		}

	private:
		bool OnVersionChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool OnEntityMeshChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

	private:
		uint32 m_version;
		MapEntity m_entity;
	};
}
