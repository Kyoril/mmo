#pragma once

#include "base/chunk_reader.h"
#include <atomic>
#include <vector>
#include <string>
#include <functional>
#include <asio/io_service.hpp>

#include "base/id_generator.h"
#include "math/aabb.h"
#include "math/quaternion.h"
#include "math/vector3.h"
#include "paging/page.h"
#include "terrain/terrain.h"
#include "game_common/world_entity_loader.h"

namespace mmo
{
	class Entity;
	class SceneNode;
	class Scene;
	class WorldModelInstance;
	class InstancedFoliage;
	class AmbientSoundEmitters;

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

		/// @brief Gets the hardware-instanced foliage (trees) renderer, or nullptr if not loaded.
		InstancedFoliage* GetInstancedFoliage() const { return m_foliage.get(); }

		void LoadPageEntities(uint8 x, uint8 y);

		void UnloadPageEntities(uint8 x, uint8 y);

		void UnloadAllEntities();

		/// @brief Whether page entity or foliage files are still being read, or world models still
		///        being prepared.
		/// @details World models are created over several dispatcher steps, so a caller that must see
		///          the finished world (the loading screen) keeps pumping the work queue and dispatcher
		///          until this returns false. Foliage files count too, so the loading screen also
		///          waits for a page's trees.
		bool HasPendingEntityLoads() const { return m_pendingEntityFiles.load() > 0 || m_pendingFoliageFiles.load() > 0 || m_pendingWorldModels > 0; }

		/// @brief Whether content that may pass a bounds test is still on its way: an entity or
		///        foliage file not handled yet (where its content lands is not known), or a world
		///        model placement still being prepared whose bounds (child world models included)
		///        pass the test. A placement whose model or child models are not all loaded yet
		///        always counts.
		/// @param isRelevant Tests a placement's world-space bounds, e.g. against a camera frustum.
		bool HasPendingWorldModels(const std::function<bool(const AABB&)>& isRelevant) const;

		/// @brief Sets where placements register the looping sounds of their meshes, or null for none.
		/// @details Placements created from then on register; unloading a placement removes its
		///          sounds again. The instance never touches the target from its destructor, so the
		///          owner clears it when it drops the world.
		void SetAmbientSounds(AmbientSoundEmitters* ambientSounds) { m_ambientSounds = ambientSounds; }

	protected:
		Entity* CreateMapEntity(const String& meshName, const Vector3& position, const Quaternion& orientation, const Vector3& scale, uint64 uniqueId);

		WorldModelInstance* CreateWorldModelEntity(const String& assetName, const Vector3& position, const Quaternion& orientation, const Vector3& scale, uint64 uniqueId, const std::vector<String>& kitPaths);

		/// @brief Removes the mesh sounds a placement registered, if any.
		void RemoveAmbientSounds(uint8 ownerKind, uint64 uniqueId) const;

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

		/// @brief A streamed world model placement whose resources are loaded one step at a time.
		struct WorldModelPreparation;

		/// @brief Starts preparing a streamed world model placement. Main thread only.
		/// @details Instantiating a world model the first time loads its file, every child world model,
		///          prop kit, mesh, material and texture, and merges its geometry - hundreds of
		///          milliseconds for a town. Each of those becomes its own dispatcher step instead, so the
		///          streaming budget spreads them over frames; the final step only creates the instance.
		void BeginWorldModelEntity(uint16 pageIndex, const WorldEntityLoader::MapEntity& entity);

		/// @brief Runs one step of a preparation and queues the next one, or creates the instance.
		void ContinueWorldModelEntity(const std::shared_ptr<WorldModelPreparation>& preparation);

		void PostPreparationStep(const std::shared_ptr<WorldModelPreparation>& preparation);

		/// @brief Loads the instanced foliage (.hfol) file for the given page, if present.
		void LoadPageFoliage(uint8 x, uint8 y);

		/// @brief Reads a page foliage file on the background thread and queues instance creation.
		void InternalLoadPageFoliage(uint16 pageIndex, const String& filename);

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
			Entity* entity;
			SceneNode* node;
		};

		struct WorldModelPlacement
		{
			uint16 pageIndex;
			std::unique_ptr<WorldModelInstance> instance;
			SceneNode* node;
		};

		std::map<uint64, EntityPlacement> m_entities;
		std::map<uint64, WorldModelPlacement> m_worldModels;

		std::set<uint16> m_loadedPages;

		/// @brief Entity files posted to the work queue whose result has not been handled on the main thread yet.
		std::atomic<uint32> m_pendingEntityFiles{ 0 };

		/// @brief Foliage files posted to the work queue whose result has not been handled on the main thread yet.
		std::atomic<uint32> m_pendingFoliageFiles{ 0 };

		/// @brief World model preparations that have neither created their instance nor been dropped yet.
		uint32 m_pendingWorldModels = 0;

		/// @brief The preparations counted by m_pendingWorldModels, for HasPendingWorldModels.
		std::map<const WorldModelPreparation*, std::weak_ptr<WorldModelPreparation>> m_preparations;

		/// @brief Hardware-instanced renderer for authored foliage (trees) loaded per page.
		std::unique_ptr<InstancedFoliage> m_foliage;

		/// @brief Receives the mesh sounds of streamed placements. Not owned; may be null.
		AmbientSoundEmitters* m_ambientSounds = nullptr;
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
