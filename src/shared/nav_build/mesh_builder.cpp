
#include "mesh_builder.h"

#include <unordered_set>

#include "DetourNavMeshBuilder.h"

#include "common.h"
#include "Recast.h"
#include "recast_context.h"

#include "assets/asset_registry.h"
#include "log/default_log_levels.h"
#include "math/aabb.h"
#include "map.h"
#include "vector_sink.h"

namespace mmo
{
    namespace settings
    {
        static constexpr int TileVoxelSize = 128; // number of voxel rows and columns per tile

        static constexpr float CellHeight = 0.25f;
        static constexpr float WalkableHeight = 1.6f; // agent height in world units (units)
        static constexpr float WalkableRadius = 0.3f; // narrowest allowable hallway in world units (units)
        static constexpr float WalkableSlope = 75.f; // maximum walkable slope, in degrees
        static constexpr float WalkableClimb = 0.5f; // maximum 'step' height for which slope is ignored (units)
        static constexpr float DetailSampleDistance = 2.0f; // heightfield detail mesh sample distance (units)
        static constexpr float DetailSampleMaxError = 0.6f; // maximum distance detail mesh surface should deviate from heightfield (units)

		// NOTE: If Recast warns "Walk towards polygon center failed to reach
		// center", try lowering this value
        static constexpr float MaxSimplificationError = 0.8f;

        static constexpr int MinRegionSize = 400;
        static constexpr int MergeRegionSize = 200;
        static constexpr int VerticesPerPolygon = 6;

        static constexpr float TileSize = terrain::constants::TileSize;
        static constexpr float CellSize = TileSize / TileVoxelSize;

        static constexpr int VoxelWalkableRadius = static_cast<int>(WalkableRadius / CellSize);
        static constexpr int VoxelWalkableHeight = static_cast<int>(WalkableHeight / CellHeight);
        static constexpr int VoxelWalkableClimb = static_cast<int>(WalkableClimb / CellHeight);
    }

	namespace
	{
        using SmartHeightFieldPtr = std::unique_ptr<rcHeightfield, decltype(&rcFreeHeightField)>;
        using SmartHeightFieldLayerSetPtr = std::unique_ptr<rcHeightfieldLayerSet, decltype(&rcFreeHeightfieldLayerSet)>;
        using SmartCompactHeightFieldPtr = std::unique_ptr<rcCompactHeightfield, decltype(&rcFreeCompactHeightfield)>;
        using SmartContourSetPtr = std::unique_ptr<rcContourSet, decltype(&rcFreeContourSet)>;
        using SmartPolyMeshPtr = std::unique_ptr<rcPolyMesh, decltype(&rcFreePolyMesh)>;
        using SmartPolyMeshDetailPtr = std::unique_ptr<rcPolyMeshDetail, decltype(&rcFreePolyMeshDetail)>;

        // multiple chunks are often required even though a tile is guaranteed to be no
        // bigger than a chunk. this is because recast requires geometry from
        // neighboring tiles to produce accurate results.
        void ComputeRequiredChunks(const Map& map, int32 tileX, int32 tileY, std::vector<TileIndex>& chunks)
        {
            chunks.clear();
			chunks.reserve(9); // 3x3 grid of chunks

            // NOTE: these are global chunk coordinates, not page or tile
            int32 const startX = (std::max)(0, tileX - 1);
            int32 const startY = (std::max)(0, tileY - 1);
            int32 const stopX = tileX + 1;
            int32 const stopY = tileY + 1;

            chunks.emplace_back(tileX, tileY);

            for (int32 y = startY; y <= stopY; ++y)
            {
                for (int32 x = startX; x <= stopX; ++x)
                {
                    int32 const pageX = x / static_cast<int32>(terrain::constants::TilesPerPage);
                    int32 const pageY = y / static_cast<int32>(terrain::constants::TilesPerPage);
                    if (pageX < 0 || pageY < 0 || pageX >= static_cast<int32>(terrain::constants::MaxPages) || pageY >= static_cast<int32>(terrain::constants::MaxPages))
                    {
                        continue;
                    }

					if (!map.HasPage(pageX, pageY))
					{
						continue;
					}

                    // put the chunk for the requested tile at the start of the results
                    if (x != tileX || y != tileY)
                    {
                        chunks.emplace_back( x, y );
                    }
                }
			}
        }

        // NOTE: this does not set bmin/bmax
        void InitializeRecastConfig(rcConfig& config)
        {
            std::memset(&config, 0, sizeof(rcConfig));

            config.cs = settings::CellSize;
            config.ch = settings::CellHeight;
            config.walkableSlopeAngle = settings::WalkableSlope;
            config.walkableClimb = settings::VoxelWalkableClimb;
            config.walkableHeight = settings::VoxelWalkableHeight;
            config.walkableRadius = settings::VoxelWalkableRadius;
            config.maxEdgeLen = config.walkableRadius * 4;
            config.maxSimplificationError = settings::MaxSimplificationError;
            config.minRegionArea = settings::MinRegionSize;
            config.mergeRegionArea = settings::MergeRegionSize;
            config.maxVertsPerPoly = settings::VerticesPerPolygon;
            config.tileSize = settings::TileVoxelSize;
            config.borderSize = config.walkableRadius + 1;
            config.width = config.tileSize + config.borderSize * 2;
            config.height = config.tileSize + config.borderSize * 2;
            config.detailSampleDist = settings::DetailSampleDistance;
            config.detailSampleMaxError = settings::DetailSampleMaxError;
        }

        void VertexToRecast(const Vector3& input, float* output)
        {
            assert(!!output);

            output[0] = input.x;
            output[1] = input.y;
            output[2] = input.z;
        }

        void VerticesToRecast(const std::vector<Vector3>& input, std::vector<float>& output)
        {
            output.resize(input.size() * 3);

            for (size_t i = 0; i < input.size(); ++i)
                VertexToRecast(input[i], &output[i * 3]);
        }

        bool TransformAndRasterize(rcContext& ctx, rcHeightfield& heightField, const float slope, const std::vector<Vector3>& vertices, const std::vector<int32>& indices, const uint8 areaFlags)
        {
            if (vertices.empty() || indices.empty())
            {
                return true;
            }

            std::vector<float> recastVertices;
            std::vector<int> cleanedIndices;
            std::vector<uint8> cleanedAreas;

            recastVertices.reserve(vertices.size() * 3);
            cleanedIndices.reserve(indices.size());
            cleanedAreas.reserve(indices.size() / 3);

            for (const Vector3& v : vertices)
            {
                recastVertices.push_back(v.x);
                recastVertices.push_back(v.y);
                recastVertices.push_back(v.z);
            }

            for (size_t i = 0; i < indices.size(); i += 3)
            {
                const Vector3& a = vertices[indices[i + 0]];
                const Vector3& b = vertices[indices[i + 1]];
                const Vector3& c = vertices[indices[i + 2]];

                // This is the critical degenerate triangle filter
                if ((b - a).Cross(c - a).GetSquaredLength() < 1e-5f)
                {
                    continue;  // Skip flat/invalid triangle
                }

                cleanedIndices.push_back(indices[i + 0]);
                cleanedIndices.push_back(indices[i + 1]);
                cleanedIndices.push_back(indices[i + 2]);
                cleanedAreas.push_back(areaFlags);
            }

            if (cleanedIndices.empty())
                return true;  // No valid triangles

            return rcRasterizeTriangles(
                &ctx, recastVertices.data(), static_cast<int>(vertices.size()), cleanedIndices.data(),
                cleanedAreas.data(), static_cast<int>(cleanedIndices.size() / 3), heightField, -1);
        }

        bool SerializeMeshTile(rcContext& ctx, const rcConfig& config, int tileX, int tileY, rcHeightfield& solid, io::Writer& out)
        {
            // initialize compact height field
            SmartCompactHeightFieldPtr chf(rcAllocCompactHeightfield(), rcFreeCompactHeightfield);

            if (!rcBuildCompactHeightfield(&ctx, config.walkableHeight,
                (std::numeric_limits<int>::max)(), solid,
                *chf))
            {
                return false;
            }

            if (!rcBuildDistanceField(&ctx, *chf))
            {
                return false;
            }

            if (!rcBuildRegions(&ctx, *chf, config.borderSize, config.minRegionArea,
                config.mergeRegionArea))
            {
                return false;
            }

            SmartContourSetPtr contourSet(rcAllocContourSet(), rcFreeContourSet);

            if (!rcBuildContours(&ctx, *chf, config.maxSimplificationError, config.maxEdgeLen, *contourSet, /*RC_CONTOUR_TESS_WALL_EDGES | RC_CONTOUR_TESS_AREA_EDGES*/0))
            {
                return false;
            }
            
            // it is possible that this tile has no navigable geometry.  in this case,
            // we 'succeed' by doing nothing further
            if (!contourSet->nconts)
            {
                return true;
            }

            SmartPolyMeshPtr polyMesh(rcAllocPolyMesh(), rcFreePolyMesh);

            if (!rcBuildPolyMesh(&ctx, *contourSet, config.maxVertsPerPoly, *polyMesh))
            {
                return false;
            }

            SmartPolyMeshDetailPtr polyMeshDetail(rcAllocPolyMeshDetail(), rcFreePolyMeshDetail);

            if (!rcBuildPolyMeshDetail(&ctx, *polyMesh, *chf, config.detailSampleDist,
                config.detailSampleMaxError, *polyMeshDetail))
            {
                return false;
            }

            chf.reset();
            contourSet.reset();

            // too many vertices?
            if (polyMesh->nverts >= 0xFFFF)
            {
                ELOG("Too many mesh vertices produced for tile (" << tileX << ", " << tileY << ")");
                return false;
            }

            for (int i = 0; i < polyMesh->npolys; ++i)
            {
                if (!polyMesh->areas[i])
                {
                    continue;
                }

                polyMesh->flags[i] = static_cast<unsigned short>(polyMesh->areas[i]);
                polyMesh->areas[i] = 0;
            }

            dtNavMeshCreateParams params = {};

            params.verts = polyMesh->verts;
            params.vertCount = polyMesh->nverts;
            params.polys = polyMesh->polys;
            params.polyAreas = polyMesh->areas;
            params.polyFlags = polyMesh->flags;
            params.polyCount = polyMesh->npolys;
            params.nvp = polyMesh->nvp;
            params.detailMeshes = polyMeshDetail->meshes;
            params.detailVerts = polyMeshDetail->verts;
            params.detailVertsCount = polyMeshDetail->nverts;
            params.detailTris = polyMeshDetail->tris;
            params.detailTriCount = polyMeshDetail->ntris;
            params.walkableHeight = settings::WalkableHeight;
            params.walkableRadius = settings::WalkableRadius;
            params.walkableClimb = settings::WalkableClimb;
            params.tileX = tileX;
            params.tileY = tileY;
            params.tileLayer = 0;
            memcpy(params.bmin, polyMesh->bmin, sizeof(polyMesh->bmin));
            memcpy(params.bmax, polyMesh->bmax, sizeof(polyMesh->bmax));
            params.cs = config.cs;
            params.ch = config.ch;
            params.buildBvTree = true;

            unsigned char* outData;
            int outDataSize;
            if (!dtCreateNavMeshData(&params, &outData, &outDataSize))
            {
                return false;
            }

			out.Sink().Write(reinterpret_cast<const char*>(outData), outDataSize);
            dtFree(outData);

            return true;
        }
	}

	void SerializableNavPage::AddTile(int32 x, int32 y, std::vector<char>&& heightField)
	{
        std::lock_guard guard(m_mutex);

        m_tiles[{x, y}] = std::move(heightField);
	}

    MeshBuilder::MeshBuilder(String outputPath, String worldName)
		: m_outputPath(std::move(outputPath))
		, m_worldPath(std::move(worldName))
		, m_chunkReferences(terrain::constants::MaxPagesSquared * terrain::constants::TilesPerPage * terrain::constants::TilesPerPage, 0)
	{
        m_map = std::make_unique<Map>(m_worldPath);

        if (!AssetRegistry::HasFile("Worlds/" + m_worldPath + "/" + m_worldPath + ".hwld"))
        {
			ELOG("World file " << m_worldPath << ".hwld does not exist!");
			return;
        }

		for (int32 y = terrain::constants::MaxPages - 1; y >= 0; --y)
		{
			for (int32 x = terrain::constants::MaxPages - 1; x >= 0; --x)
			{
                std::stringstream strm;
				strm << "Worlds/" << m_worldPath << "/" << m_worldPath << "/Terrain/" << std::setfill('0') << std::setw(2) << x << "_" << std::setfill('0') << std::setw(2) << y << ".tile";

				if (!AssetRegistry::HasFile(strm.str()))
				{
					continue;
				}

                for (int32 tileY = 0; tileY < terrain::constants::TilesPerPage; ++tileY)
                {
					for (int32 tileX = 0; tileX < terrain::constants::TilesPerPage; ++tileX)
					{
						int32 const globalTileX = x * terrain::constants::TilesPerPage + tileX;
						int32 const globalTileY = y * terrain::constants::TilesPerPage + tileY;

						std::vector<TileIndex> chunks;
						ComputeRequiredChunks(*m_map, globalTileX, globalTileY, chunks);

						for (auto const& chunk : chunks)
						{
							AddChunkReference(chunk.x, chunk.y);
						}
						
						m_pendingTiles.push_back(TileIndex{ globalTileX, globalTileY });
					}
                }
			}
		}

        m_totalTiles = m_pendingTiles.size();
	}

	bool MeshBuilder::GetNextTile(TileIndex& tile)
	{
        std::lock_guard guard(m_mutex);

        if (m_pendingTiles.empty())
        {
            return false;
        }

        auto const back = m_pendingTiles.back();
        m_pendingTiles.pop_back();

        tile = back;

        return true;
	}

	float MeshBuilder::PercentComplete() const
	{
		return 100.f * (static_cast<float>(m_completedTiles) / static_cast<float>(m_totalTiles));
	}

	bool MeshBuilder::BuildAndSerializeTerrainTile(const TileIndex& tile)
	{
        float minY = std::numeric_limits<float>::max(), maxY = std::numeric_limits<float>::lowest();

        // regardless of tile size, we only need the surrounding page chunks
        std::vector<const TerrainChunk*> chunks;
        std::vector<TileIndex> chunkPositions;

        ComputeRequiredChunks(*m_map, tile.x, tile.y, chunkPositions);

        chunks.reserve(chunkPositions.size());
        for (auto const& chunkPosition : chunkPositions)
        {
            auto const pageX = chunkPosition.x / terrain::constants::TilesPerPage;
            auto const pageY = chunkPosition.y / terrain::constants::TilesPerPage;
            auto const chunkX = chunkPosition.x % terrain::constants::TilesPerPage;
            auto const chunkY = chunkPosition.y % terrain::constants::TilesPerPage;

            ASSERT(pageX >= 0 && pageY >= 0 && pageX < terrain::constants::MaxPages && pageY < terrain::constants::MaxPages);

			auto const page = m_map->GetPage(pageX, pageY);
            auto const chunk = page->GetChunk(chunkX, chunkY);

            minY = (std::min)(minY, chunk->m_minY);
            maxY = (std::max)(maxY, chunk->m_maxY);

            chunks.push_back(chunk);
        }

        // because ComputeRequiredChunks places the chunk that this tile falls on at
        // the start of the collection, we know that the first element in the
        // 'chunks' collection is also the chunk upon which this tile falls.
        auto const tileChunk = chunks[0];

        rcConfig config;
        InitializeRecastConfig(config);

        config.bmin[0] = tile.x * settings::TileSize - 32.f * terrain::constants::PageSize;
        config.bmin[1] = minY;
        config.bmin[2] = tile.y * settings::TileSize - 32.f * terrain::constants::PageSize;

        config.bmax[0] = (tile.x + 1) * settings::TileSize - 32.f * terrain::constants::PageSize;
        config.bmax[1] = maxY;
        config.bmax[2] = (tile.y + 1) * settings::TileSize - 32.f * terrain::constants::PageSize;

		if (minY > maxY)
		{
			WLOG("Tile " << tile.x << ", " << tile.y << " has higher minY value than maxY value!");
			return false;
		}

        // erode mesh tile boundaries to force recast to examine obstacles on or near the tile boundary
		const float erodedMin[] = {
			config.bmin[0] - config.borderSize * config.cs,
			config.bmin[1],
			config.bmin[2] - config.borderSize * config.cs
		};
        const float erodedMax[] = {
			config.bmax[0] + config.borderSize * config.cs,
			config.bmax[1],
			config.bmax[2] + config.borderSize * config.cs
        };

        RecastContext ctx(RC_LOG_WARNING);

        SmartHeightFieldPtr solid(rcAllocHeightfield(), rcFreeHeightField);

        if (!rcCreateHeightfield(&ctx, *solid, config.width, config.height, erodedMin, erodedMax, config.cs, config.ch))
        {
            return false;
        }

        std::unordered_set<std::uint32_t> rasterizedEntities;

        // incrementally rasterize mesh geometry into the height field, setting poly flags as appropriate
        for (auto const& chunk : chunks)
        {
            // Terrain
            if (!TransformAndRasterize(ctx, *solid, config.walkableSlopeAngle, chunk->m_terrainVertices, chunk->m_terrainIndices, poly_flags::Ground))
            {
                return false;
            }

            // Map Entities
            for (auto const& wmoId : chunk->m_mapEntityInstances)
            {
                if (rasterizedEntities.contains(wmoId))
                    continue;

                auto const entityInstance = m_map->GetMapEntityInstance(wmoId);
                ASSERT(entityInstance);

                std::vector<Vector3> vertices;
                std::vector<int32> indices;

                entityInstance->BuildTriangles(vertices, indices);
                if (!TransformAndRasterize(ctx, *solid, config.walkableSlopeAngle,
                    vertices, indices, poly_flags::Entity))
                    return false;

                rasterizedEntities.insert(wmoId);
            }
        }

    	rcFilterLedgeSpans(&ctx, config.walkableHeight, config.walkableClimb, *solid);

        rcFilterWalkableLowHeightSpans(&ctx, config.walkableHeight, *solid);
        rcFilterLowHangingWalkableObstacles(&ctx, config.walkableClimb, *solid);

        // serialize heightfield for this tile
		std::vector<char> heightFieldData;
		io::VectorSink heightFieldSink{ heightFieldData };
        io::Writer heightFieldWriter{ heightFieldSink };

        // Placeholder for mesh size
        const size_t meshSizePos = heightFieldSink.Position();
		heightFieldWriter << io::write<uint32>(0);

        // Serialize final navmesh tile
        const size_t meshStartPos = heightFieldSink.Position();
        auto const result = SerializeMeshTile(ctx, config, tile.x, tile.y, *solid, heightFieldWriter);

        // Write final mesh size
        const size_t meshEndPos = heightFieldSink.Position();
		const uint32 meshSize = static_cast<uint32>(meshEndPos - meshStartPos);
		heightFieldSink.Overwrite(meshSizePos, reinterpret_cast<const char*>(&meshSize), sizeof(uint32));

        {
            std::lock_guard guard(m_mutex);

            auto const pageX = tile.x / terrain::constants::TilesPerPage;
            auto const pageY = tile.y / terrain::constants::TilesPerPage;
            auto const localTileX = tile.x % terrain::constants::TilesPerPage;
            auto const localTileY = tile.y % terrain::constants::TilesPerPage;

            SerializableNavPage* page = GetInProgressPage(pageX, pageY);
            page->AddTile(localTileX, localTileY, std::move(heightFieldData));

            if (page->IsComplete())
            {
                std::stringstream str;
                str << std::setw(2) << std::setfill('0') << pageX << "_"
                    << std::setw(2) << std::setfill('0') << pageY << ".nav";

				create_directories(std::filesystem::path(m_outputPath) / "nav" / m_map->Name);

                {
                    std::ofstream file(std::filesystem::path(m_outputPath) / "nav" / m_map->Name / str.str(), std::ios::binary | std::ios::trunc);
                    ASSERT(!file.bad());

                    io::StreamSink sink{ file };
                    io::Writer writer{ sink };
                    page->Serialize(writer);
                }
				
				DLOG("Finished " << m_map->Name << " Page (" << pageX << ", " << pageY << ")");

                RemovePage(page);
            }
        }

        ++m_completedTiles;

        for (const auto& [x, y] : chunkPositions)
        {
            RemoveChunkReference(x, y);
        }

        return true;
	}

	void MeshBuilder::SaveMap() const
	{
		const String path = (std::filesystem::path(m_outputPath) / "nav" / m_map->Name).string() + ".map";
        std::ofstream of(path, std::ofstream::binary | std::ofstream::trunc);

		io::StreamSink sink{ of };
		io::Writer writer{ sink };

        m_map->Serialize(writer);
	}

	void MeshBuilder::AddChunkReference(const int32 chunkX, const int32 chunkY)
	{
        ASSERT(chunkX >= 0 && chunkX < terrain::constants::MaxPages * terrain::constants::TilesPerPage);
        ASSERT(chunkY >= 0 && chunkY < terrain::constants::MaxPages * terrain::constants::TilesPerPage);

        ++m_chunkReferences[chunkY * terrain::constants::MaxPages * terrain::constants::TilesPerPage + chunkX];
	}

	void MeshBuilder::RemoveChunkReference(const int32 chunkX, const int32 chunkY)
	{
        auto const pageX = chunkX / terrain::constants::TilesPerPage;
        auto const pageY = chunkY / terrain::constants::TilesPerPage;

        std::lock_guard guard(m_mutex);
        --m_chunkReferences[chunkY * terrain::constants::MaxPages * terrain::constants::TilesPerPage + chunkX];

        // Check to see if all chunks on this page are without references. If so, unload the page.
        bool unload = true;
        for (int x = 0; x < terrain::constants::TilesPerPage; ++x)
        {
            for (int y = 0; y < terrain::constants::TilesPerPage; ++y)
            {
                auto const globalChunkX = pageX * terrain::constants::TilesPerPage + x;
                auto const globalChunkY = pageY * terrain::constants::TilesPerPage + y;

                if (m_chunkReferences[globalChunkY * terrain::constants::MaxPages * terrain::constants::TilesPerPage + globalChunkX] > 0)
                {
                    unload = false;
                    break;
                }
            }
        }

        if (unload)
        {
			m_map->UnloadPage(pageX, pageY);
        }
	}

	SerializableNavPage* MeshBuilder::GetInProgressPage(int32 x, int32 y)
	{
        if (!m_pagesInProgress[{x, y}])
        {
            m_pagesInProgress[{x, y}] = std::make_unique<SerializableNavPage>(x, y);
        }

        return m_pagesInProgress[{x, y}].get();
	}

	void MeshBuilder::RemovePage(const SerializableNavPage* page)
	{
        for (const auto& [index, p] : m_pagesInProgress)
        {
            if (p.get() == page)
            {
                m_pagesInProgress.erase(index);
                return;
            }
        }
	}
}
