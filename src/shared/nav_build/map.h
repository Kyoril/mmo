#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "math/vector3.h"
#include "math/aabb.h"
#include "terrain/constants.h"

#include <memory>
#include <vector>
#include <mutex>

namespace mmo
{
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

        friend TerrainPage::TerrainPage(const Map*, int, int);

    };

}