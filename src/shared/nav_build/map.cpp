
#include "map.h"

#include "assets/asset_registry.h"

#include <iomanip>
#include <sstream>

namespace mmo
{
	TerrainPage::TerrainPage(const Map* map, int x, int y)
        : map(map)
        , X(x)
        , Y(y)
    {
        const float minX = static_cast<float>(static_cast<double>(X - 32) * terrain::constants::PageSize);
        const float minZ = static_cast<float>(static_cast<double>(Y - 32) * terrain::constants::PageSize);
        Bounds = AABB(
            Vector3(minX, std::numeric_limits<float>::max(), minZ),
            Vector3(minX + terrain::constants::PageSize, std::numeric_limits<float>::lowest(), minZ + terrain::constants::PageSize));

        // TODO: Read page file and initialize chunks
        for (uint32 y = 0; y < terrain::constants::TilesPerPage; ++y)
        {
            for (uint32 x = 0; x < terrain::constants::TilesPerPage; ++x)
            {
                m_chunks[y][x] = std::make_unique<TerrainChunk>();
                m_chunks[y][x]->m_minY = std::numeric_limits<float>::max();
                m_chunks[y][x]->m_maxY = std::numeric_limits<float>::lowest();
                m_chunks[y][x]->m_areaId = 0;
                m_chunks[y][x]->m_zoneId = 0;
                m_chunks[y][x]->m_terrainVertices.resize(terrain::constants::VerticesPerTile * terrain::constants::VerticesPerTile, Vector3::Zero);
                m_chunks[y][x]->m_terrainIndices.resize(terrain::constants::VerticesPerTile * terrain::constants::VerticesPerTile, 0);

                std::memset(&m_chunks[y][x]->m_heights, 0, sizeof(m_chunks[y][x]->m_heights));
            }
        }
    }

    Map::Map(std::string mapName)
        : Name(std::move(mapName))
        , Id(0)
    {
        // TODO: Load map file (hwld)

        for (uint32 y = 0; y < terrain::constants::MaxPages; ++y)
        {
            for (uint32 x = 0; x < terrain::constants::MaxPages; ++x)
            {
                std::stringstream strm;
                strm << "Worlds/" << Name << "/" << Name << "/" << std::setfill('0') << std::setw(2) << x << "_" << std::setfill('0') << std::setw(2) << y << ".tile";
                m_hasPage[x][y] = AssetRegistry::HasFile(strm.str());

                // TODO: Load m_hasTerrain from hwld file
                if (m_hasPage[x][y])
                {
                    m_hasTerrain = true;
                }
            }
        }
    }

    bool Map::HasPage(int x, int y) const
    {
        ASSERT(x >= 0 && y >= 0 && x < terrain::constants::MaxPages && y < terrain::constants::MaxPages);

        return m_hasPage[x][y];
    }

    const TerrainPage* Map::GetPage(int x, int y)
    {
        ASSERT(x >= 0 && y >= 0 && x < terrain::constants::MaxPages && y < terrain::constants::MaxPages);

        std::lock_guard guard(m_pageMutex);

        if (!m_hasPage[x][y])
            return nullptr;

        if (!m_pages[x][y])
            m_pages[x][y] = std::make_unique<TerrainPage>(this, x, y);

        return m_pages[x][y].get();
    }

    void Map::UnloadPage(int x, int y)
    {
        std::lock_guard guard(m_pageMutex);
        m_pages[x][y].reset();
    }

}
