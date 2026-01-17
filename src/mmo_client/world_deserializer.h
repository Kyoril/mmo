#pragma once

#include "base/chunk_reader.h"
#include <vector>
#include <string>
#include <functional>
#include <asio/io_service.hpp>

#include "base/id_generator.h"
#include "math/quaternion.h"
#include "math/vector3.h"
#include "paging/page.h"
#include "terrain/terrain.h"

namespace mmo
{
	class Entity;
	class SceneNode;
	class Scene;
	class Light;

	namespace world_version
	{
		enum Type
		{
			Latest = -1,

			Version_0_0_0_1 = 0x0001,

			Version_0_0_0_2 = 0x0002,

			Version_0_0_0_3 = 0x0003,
		};
	}

	typedef world_version::Type WorldVersion;

	class ClientWorldInstance : public std::enable_shared_from_this<ClientWorldInstance>
	{
	public:
		friend class ClientWorldInstanceDeserializer;

	public:
		explicit ClientWorldInstance(Scene& scene, SceneNode& rootNode, const String& name, asio::io_service& workQueue, asio::io_service& dispatcher);
		~ClientWorldInstance();

		bool HasTerrain() const { return m_terrain != nullptr; }

		terrain::Terrain* GetTerrain() const { return m_terrain.get(); }

		void LoadPageEntities(uint8 x, uint8 y);

		void UnloadPageEntities(uint8 x, uint8 y);

		void UnloadAllEntities();

	protected:
		Entity* CreateMapEntity(const String& meshName, const Vector3& position, const Quaternion& orientation, const Vector3& scale, uint64 uniqueId);

		Light* CreatePointLight(const Vector3& position, const Vector4& color, float intensity, float range, uint64 uniqueId);

		static uint16 BuildPageIndex(uint8 x, const uint8 y)
		{
			return (x << 8) | y;
		}

		static PagePosition GetPagePosition(const Vector3& pos)
		{
			return PagePosition(static_cast<uint32>(
				floor(pos.x / terrain::constants::PageSize)) + 32,
				static_cast<uint32>(floor(pos.z / terrain::constants::PageSize)) + 32);
		}

		void InternalLoadPageEntity(uint16 pageIndex, const String& filename);

	private:
		asio::io_service& m_workQueue;
		asio::io_service& m_dispatcher;
		String m_name;
		Scene& m_scene;
		SceneNode& m_rootNode;
		IdGenerator<uint64> m_entityIdGenerator{ 1 };
		std::unique_ptr<terrain::Terrain> m_terrain;

		struct EntityPlacement
		{
			uint16 pageIndex;
			Entity* entity { nullptr };
			Light* light { nullptr };
			SceneNode* node;
		};

		std::map<uint64, EntityPlacement> m_entities;

		std::set<uint16> m_loadedPages;
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

		bool ReadEntityChunkV2(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadTerrainChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

	private:

		struct MapEntityChunkContent
		{
			uint64 uniqueId;
			uint32 meshNameIndex;
			Vector3 position;
			Quaternion rotation;
			Vector3 scale;
		};

		uint32 m_version;
		ClientWorldInstance& m_world;
		std::vector<String> m_meshNames;
	};
}
