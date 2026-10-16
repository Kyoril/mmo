#pragma once

#include "base/chunk_reader.h"
#include "binary_io/writer.h"
#include "math/vector3.h"
#include "math/quaternion.h"

namespace mmo
{
	uint64 GenerateUniqueId();

	/// @brief Writes the optional prop kit list chunk (WKIT) of a world model placement.
	///        Writes nothing if the list is empty. Payload: u8 count, then str16 paths.
	///        Must be called after the WWMO chunk was finished; older readers skip the chunk.
	/// @param writer Writer to append the chunk to.
	/// @param kitPaths Ordered prop kit asset paths (at most 255 are written).
	void WriteWorldModelKitChunk(io::Writer& writer, const std::vector<String>& kitPaths);

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
			/// @brief Ordered prop kit paths (world models only, from the optional WKIT chunk).
			std::vector<String> kitPaths;
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

		bool OnKitChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

	private:
		uint32 m_version;
		MapEntity m_entity;
	};

	/// @brief Writes a complete world model placement file (WVER, WWMO and, if the entity has kits, WKIT).
	///        This is the single writer shared by the world editor's Save and unattended jobs.
	/// @param writer Writer to write the file content to.
	/// @param entity The world model placement (uses meshName as the .hwmo path, position, rotation,
	///        scale, uniqueId, name, category and kitPaths).
	void WriteWorldModelPlacement(io::Writer& writer, const WorldEntityLoader::MapEntity& entity);
}
