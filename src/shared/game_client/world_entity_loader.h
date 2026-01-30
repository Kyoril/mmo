#pragma once

#include "base/chunk_reader.h"
#include "math/vector3.h"
#include "math/quaternion.h"

namespace mmo
{
	uint64 GenerateUniqueId();

	/// @brief Type of world entity.
	enum class WorldEntityType : uint8
	{
		/// @brief Regular mesh entity.
		Mesh = 0,
		/// @brief World model object.
		WorldModel = 1
	};

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
			WorldEntityType entityType = WorldEntityType::Mesh;
			String meshName;  ///< For Mesh type, this is the mesh file. For WorldModel type, this is the hwmo file.
			Vector3 position;
			Quaternion rotation;
			Vector3 scale;
			uint64 uniqueId;
			std::vector<MaterialOverride> materialOverrides;
			String name;
			String category;
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

		bool OnWorldModelChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

	private:
		uint32 m_version;
		MapEntity m_entity;
	};
}
