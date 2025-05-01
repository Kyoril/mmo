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
        float m_heights[terrain::constants::VerticesPerTile * terrain::constants::VerticesPerTile];

        std::vector<Vector3> m_terrainVertices;
        std::vector<int32> m_terrainIndices;

        std::vector<Vector3> m_liquidVertices;
        std::vector<int32> m_liquidIndices;

        std::vector<uint32> m_mapEntityInstances;

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
            const AABB& bounds,
            const Matrix4& transformMatrix);

        Vector3 TransformVertex(const Vector3& vertex) const;

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

        friend TerrainPage::TerrainPage(const Map*, int, int);

    };

}
