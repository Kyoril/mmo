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

	class Map final : public NonCopyable
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

		void Serialize(io::Writer& writer) const;

    private:
        bool m_hasPage[terrain::constants::MaxPages][terrain::constants::MaxPages];
        bool m_hasTerrain;

        mutable std::mutex m_pageMutex;
        std::unique_ptr<TerrainPage> m_pages[terrain::constants::MaxPages][terrain::constants::MaxPages];

        mutable std::mutex m_mapEntityMutex;
        std::vector<std::unique_ptr<const MapEntity>> m_loadedMapEntities;
        std::map<uint32, std::unique_ptr<const MapEntityInstance>> m_loadedMapEntityInstances;

        friend TerrainPage::TerrainPage(const Map*, int, int);

    };

}
