#pragma once

#include "base/chunk_reader.h"
#include "math/vector3.h"
#include "math/quaternion.h"
#include "math/vector4.h"

namespace mmo
{
	uint64 GenerateUniqueId();

	/// @brief Enumeration of world entity types.
	enum class WorldEntityType : uint8
	{
		/// @brief A static mesh entity.
		Mesh = 0,

		/// @brief A point light entity.
		PointLight = 1,
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
			WorldEntityType type { WorldEntityType::Mesh };
			String meshName;
			Vector3 position;
			Quaternion rotation;
			Vector3 scale;
			uint64 uniqueId;
			std::vector<MaterialOverride> materialOverrides;
			String name;
			String category;

			// Light-specific properties
			Vector4 lightColor { 1.0f, 1.0f, 1.0f, 1.0f };
			float lightIntensity { 1.0f };
			float lightRange { 10.0f };
		};

	public:
		explicit WorldEntityLoader();

		const MapEntity& GetEntity() const
		{
			return m_entity;
		}

	private:
		bool OnVersionChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool OnEntityTypeChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool OnEntityMeshChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool OnEntityLightChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

	private:
		uint32 m_version;
		MapEntity m_entity;
	};
}
