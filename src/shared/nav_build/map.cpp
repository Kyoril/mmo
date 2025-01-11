
#include "map.h"

#include "assets/asset_registry.h"
#include "terrain/terrain.h"
#include "terrain/page.h"
#include "scene_graph/scene.h"

#include <iomanip>
#include <sstream>


namespace mmo
{
    uint16 GetIndex(size_t x, size_t y)
    {
        return static_cast<uint16>(x + y * terrain::constants::VerticesPerTile);
    }

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

        // TODO: This method is lazy as it loads the page, but the page also has references to visual stuff which is really not needed here
        // We should separate page loading and data holding from the visual representation!
        Scene scene;
        terrain::Terrain terrain(scene, nullptr, terrain::constants::MaxPages, terrain::constants::MaxPages);
        terrain.SetBaseFileName("Worlds/" + map->Name + "/" + map->Name);

        terrain::Page* page = terrain.GetPage(x, y);
        ASSERT(page);

        const bool pagePrepared = page->Prepare();
        ASSERT(pagePrepared);

        for (uint32 y = 0; y < terrain::constants::TilesPerPage; ++y)
        {
            for (uint32 x = 0; x < terrain::constants::TilesPerPage; ++x)
            {
                // Initialize the terrain chunk
                m_chunks[y][x] = std::make_unique<TerrainChunk>();
                m_chunks[y][x]->m_minY = std::numeric_limits<float>::max();
                m_chunks[y][x]->m_maxY = std::numeric_limits<float>::lowest();
                m_chunks[y][x]->m_areaId = 0;
                m_chunks[y][x]->m_zoneId = 0;
                m_chunks[y][x]->m_terrainVertices.reserve(terrain::constants::VerticesPerTile * terrain::constants::VerticesPerTile);
                m_chunks[y][x]->m_terrainIndices.reserve(terrain::constants::VerticesPerTile * terrain::constants::VerticesPerTile * 6);

                // Fill the terrain chunk vertex, index and height data
                const size_t startX = x * (terrain::constants::VerticesPerTile - 1);
                const size_t startZ = y * (terrain::constants::VerticesPerTile - 1);
                const size_t endX = startX + terrain::constants::VerticesPerTile;
                const size_t endZ = startZ + terrain::constants::VerticesPerTile;

                const float scale = terrain::constants::TileSize / (terrain::constants::VerticesPerTile - 1);

                for (size_t j = startZ; j < endZ; ++j)
                {
                    for (size_t i = startX; i < endX; ++i)
                    {
                        const float height = page->GetHeightAt(i, j);

                        Vector3 position = Vector3(scale * i, height, scale * j);

                        // TODO: Convert this position to the global position!
                        position.x += Bounds.min.x;
                        position.z += Bounds.min.z;

                        if (height < m_chunks[y][x]->m_minY) {
                            m_chunks[y][x]->m_minY = height;
                        }

                        if (height > m_chunks[y][x]->m_maxY) {
                            m_chunks[y][x]->m_maxY = height;
                        }

                        if (height < Bounds.min.y)
                        {
                            Bounds.min.y = height;
                        }
                        if (height > Bounds.max.y)
                        {
                            Bounds.max.y = height;
                        }

                        // Save height and vertex
                        m_chunks[y][x]->m_terrainVertices.push_back(position);
						m_chunks[y][x]->m_heights[(j - startZ) + (i - startX) * terrain::constants::VerticesPerTile] = height;

						if (j != endZ - 1 && i != endX - 1)
						{
                            // triangles
                            m_chunks[y][x]->m_terrainIndices.push_back(GetIndex(i - startX, j - startZ));
                            m_chunks[y][x]->m_terrainIndices.push_back(GetIndex(i - startX, j - startZ + 1));
                            m_chunks[y][x]->m_terrainIndices.push_back(GetIndex(i - startX + 1, j - startZ));

                            m_chunks[y][x]->m_terrainIndices.push_back(GetIndex(i - startX, j - startZ + 1));
                            m_chunks[y][x]->m_terrainIndices.push_back(GetIndex(i - startX + 1, j - startZ + 1));
                            m_chunks[y][x]->m_terrainIndices.push_back(GetIndex(i - startX + 1, j - startZ));
						}
                    }
                }

                for (const auto& index : m_chunks[y][x]->m_terrainIndices)
                {
					ASSERT(index < m_chunks[y][x]->m_terrainVertices.size());
                }
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

    void Map::Serialize(io::Writer& writer) const
    {
        writer << io::write<uint32>('MAP1');

        if (m_hasTerrain)
        {
            writer << io::write<uint8>(1);

            uint8 hasPageMap[sizeof(m_hasPage) / 8] = {};
            for (auto y = 0; y < terrain::constants::MaxPages; ++y)
            {
                for (auto x = 0; x < terrain::constants::MaxPages; ++x)
                {
                    if (!m_hasPage[x][y])
                    {
                        continue;
                    }

                    auto const offset = y * terrain::constants::MaxPages + x;
                    auto const byte_offset = offset / 8;
                    auto const bit_offset = offset % 8;

                    hasPageMap[byte_offset] |= (1 << bit_offset);
                }
            }

            writer.WritePOD(hasPageMap);
        }
    }
}
