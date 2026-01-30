#pragma once

#include <map>

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "math/vector3.h"
#include "math/aabb.h"
#include "terrain/constants.h"

#include <memory>
#include <vector>
#include <mutex>
#include <set>

#include "base/chunk_reader.h"

namespace mmo
{
#pragma pack(push, 1)
    struct PageChunkLocation
    {
        uint8 PageX;
        uint8 PageY;
        uint8 ChunkX;
        uint8 ChunkY;

        bool operator<(const PageChunkLocation& other) const
        {
            return *reinterpret_cast<const uint32*>(this) <
                *reinterpret_cast<const uint32*>(&other);
        }
    };
#pragma pack(pop)

    static_assert(sizeof(PageChunkLocation) == 4, "Unexpected packing of PageChunkLocation");

    struct TerrainChunk
    {
        // Store only outer grid heights per tile (9x9)
        float m_heights[terrain::constants::OuterVerticesPerTileSide * terrain::constants::OuterVerticesPerTileSide];

        std::vector<Vector3> m_terrainVertices;
        std::vector<int32> m_terrainIndices;

        std::vector<Vector3> m_liquidVertices;
        std::vector<int32> m_liquidIndices;

        std::vector<uint32> m_mapEntityInstances;
        std::vector<uint64> m_worldModelInstances;

        uint32 m_areaId = 0;
        uint32 m_zoneId = 0;

        float m_minY;
        float m_maxY;
    };

    struct MapEntity
    {
        uint32 RootId;
        std::vector<Vector3> Vertices;
        std::vector<int32> Indices;
        String Filename;
        AABB Bounds;

        MapEntity(const std::string& path);
    };


    class MapEntityInstance
    {
    public:
        const Matrix4 TransformMatrix;
        AABB Bounds;

        const MapEntity* const Model;

        std::set<PageChunkLocation> PageChunks;

        MapEntityInstance(const MapEntity* entity,
                          AABB bounds,
            const Matrix4& transformMatrix);

        Vector3 TransformVertex(const Vector3& vertex) const;

        void BuildTriangles(std::vector<Vector3>& vertices, std::vector<int32>& indices) const;
    };

    /// @brief Represents a world model entity for navigation mesh building.
    struct WorldModelEntity
    {
        /// @brief Path to the world model file.
        String Filename;

        /// @brief Bounds of the world model.
        AABB Bounds;

        /// @brief Collision triangles - vertices for each group's meshes.
        std::vector<Vector3> Vertices;

        /// @brief Collision triangles - indices for each group's meshes.
        std::vector<int32> Indices;

        /// @brief Constructs a WorldModelEntity from a .hwmo file path.
        /// @param path Path to the world model file.
        explicit WorldModelEntity(const std::string& path);
    };

    /// @brief Represents a placed instance of a world model entity in the world.
    class WorldModelEntityInstance
    {
    public:
        /// @brief Transform matrix for this instance.
        const Matrix4 TransformMatrix;

        /// @brief World-space bounds of this instance.
        AABB Bounds;

        /// @brief Pointer to the world model entity data.
        const WorldModelEntity* const Model;

        /// @brief Set of page chunks this instance overlaps.
        std::set<PageChunkLocation> PageChunks;

        /// @brief Constructs a WorldModelEntityInstance.
        /// @param entity The world model entity.
        /// @param bounds The world-space bounds.
        /// @param transformMatrix The transform matrix.
        WorldModelEntityInstance(
            const WorldModelEntity* entity,
            AABB bounds,
            const Matrix4& transformMatrix);

        /// @brief Transforms a vertex from model space to world space.
        /// @param vertex The vertex to transform.
        /// @return The transformed vertex.
        Vector3 TransformVertex(const Vector3& vertex) const;

        /// @brief Builds transformed triangles for this instance.
        /// @param vertices Output vertices in world space.
        /// @param indices Output indices.
        void BuildTriangles(std::vector<Vector3>& vertices, std::vector<int32>& indices) const;
    };

    class Map;

    class TerrainPage
    {
    private:
        std::unique_ptr<TerrainChunk> m_chunks[terrain::constants::TilesPerPage][terrain::constants::TilesPerPage];

    public:
        const Map* map;
        const int X;
        const int Y;

        AABB Bounds;

        TerrainPage(const Map* map, int x, int y);

        const TerrainChunk* GetChunk(int chunkX, int chunkY) const { return m_chunks[chunkY][chunkX].get(); }
    };

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
	class Map final
		: public ChunkReader
    {
    public:
        const std::string Name;
        const unsigned int Id;

    public:
        explicit Map(std::string mapName);
		~Map() override = default;

	public:
        bool HasPage(int32 x, int32 y) const;

        const TerrainPage* GetPage(int32 x, int32 y);

        void UnloadPage(int32 x, int32 y);

        const MapEntity* GetMapEntity(const std::string& name);

        void InsertMapEntityInstance(unsigned int uniqueId, std::unique_ptr<MapEntityInstance> instance);

        const MapEntityInstance* GetMapEntityInstance(unsigned int uniqueId) const;

        void GetMapEntityInstancesInArea(const AABB& bounds, std::vector<uint32>& out_instanceIds) const;

        /// @brief Gets or loads a world model entity by path.
        /// @param name Path to the world model file.
        /// @return Pointer to the world model entity.
        const WorldModelEntity* GetWorldModelEntity(const std::string& name);

        /// @brief Inserts a world model entity instance.
        /// @param uniqueId Unique ID for the instance.
        /// @param instance The instance to insert.
        void InsertWorldModelEntityInstance(uint64 uniqueId, std::unique_ptr<WorldModelEntityInstance> instance);

        /// @brief Gets a world model entity instance by ID.
        /// @param uniqueId The unique ID.
        /// @return Pointer to the instance, or nullptr if not found.
        const WorldModelEntityInstance* GetWorldModelEntityInstance(uint64 uniqueId) const;

        /// @brief Gets world model entity instances that intersect with the given bounds.
        /// @param bounds The bounds to check.
        /// @param out_instanceIds Output vector of instance IDs.
        void GetWorldModelEntityInstancesInArea(const AABB& bounds, std::vector<uint64>& out_instanceIds) const;

		bool HasTerrain() const { return m_hasTerrain; }

		void Serialize(io::Writer& writer) const;

	private:
        bool ReadVersionChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

        bool ReadMeshNamesChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

        bool ReadEntityChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

        bool ReadEntityChunkV2(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

        bool ReadTerrainChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

    private:
        uint32 m_version = 0;

        struct MapEntityChunkContent
        {
            uint32 uniqueId;
            uint32 meshNameIndex;
            Vector3 position;
            Quaternion rotation;
            Vector3 scale;
        };

        std::vector<String> m_meshNames;

        bool m_hasPage[terrain::constants::MaxPages][terrain::constants::MaxPages];
        bool m_hasTerrain = false;

        mutable std::mutex m_pageMutex;
        std::unique_ptr<TerrainPage> m_pages[terrain::constants::MaxPages][terrain::constants::MaxPages];

        mutable std::mutex m_mapEntityMutex;
        std::vector<std::unique_ptr<const MapEntity>> m_loadedMapEntities;
        std::map<uint32, std::unique_ptr<const MapEntityInstance>> m_loadedMapEntityInstances;

        mutable std::mutex m_worldModelMutex;
        std::vector<std::unique_ptr<const WorldModelEntity>> m_loadedWorldModelEntities;
        std::map<uint64, std::unique_ptr<const WorldModelEntityInstance>> m_loadedWorldModelEntityInstances;

        friend TerrainPage::TerrainPage(const Map*, int, int);

    };

}
