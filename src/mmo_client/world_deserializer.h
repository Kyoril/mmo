#pragma once

#include "base/chunk_reader.h"
#include <vector>
#include <string>

#include "base/id_generator.h"
#include "math/quaternion.h"
#include "math/vector3.h"
#include "terrain/terrain.h"

namespace mmo
{
	class SceneNode;
	class Scene;

	namespace world_version
	{
		enum Type
		{
			Latest = -1,

			Version_0_0_0_1 = 0x0001,
		};
	}

	typedef world_version::Type WorldVersion;

	class ClientWorldInstance
	{
	public:
		friend class ClientWorldInstanceDeserializer;

	public:
		explicit ClientWorldInstance(Scene& scene, SceneNode& rootNode, const String& name);

		bool HasTerrain() const { return m_terrain != nullptr; }

		terrain::Terrain* GetTerrain() const { return m_terrain.get(); }

	protected:
		void CreateMapEntity(const String& meshName, const Vector3& position, const Quaternion& orientation, const Vector3& scale);

	private:
		String m_name;
		Scene& m_scene;
		SceneNode& m_rootNode;
		IdGenerator<uint64> m_entityIdGenerator{ 1 };
		std::unique_ptr<terrain::Terrain> m_terrain;
	};

	/// @brief Supports deserializing a world from a file.
	class ClientWorldInstanceDeserializer final : public ChunkReader
	{
	public:
		explicit ClientWorldInstanceDeserializer(ClientWorldInstance& world);
		~ClientWorldInstanceDeserializer() override = default;

	protected:
		bool ReadVersionChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadMeshNamesChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadEntityChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadTerrainChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

	private:

		struct MapEntityChunkContent
		{
			uint32 uniqueId;
			uint32 meshNameIndex;
			Vector3 position;
			Quaternion rotation;
			Vector3 scale;
		};

		ClientWorldInstance& m_world;
		std::vector<String> m_meshNames;
	};
}
