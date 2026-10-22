// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "glue_terrain_baker.h"

#include "glue_mesh_writer.h"

#include "editors/world_editor/page_bake_utils.h"
#include "editors/world_editor/world_editor_instance.h"

#include "binary_io/stream_sink.h"
#include "binary_io/writer.h"
#include "game_common/glue_terrain_mesh.h"
#include "graphics/material_instance.h"
#include "log/default_log_levels.h"
#include "scene_graph/material_instance_serializer.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/mesh_merge.h"
#include "terrain/constants.h"
#include "terrain/page.h"
#include "terrain/tile.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

namespace mmo
{
	static_assert(glue_terrain::OuterVerticesPerTileSide == terrain::constants::OuterVerticesPerTileSide, "glue terrain grid must match the terrain");
	static_assert(glue_terrain::InnerVerticesPerTileSide == terrain::constants::InnerVerticesPerTileSide, "glue terrain grid must match the terrain");
	static_assert(glue_terrain::TilesPerPage == terrain::constants::TilesPerPage, "glue terrain grid must match the terrain");
	static_assert(glue_terrain::TileSize == terrain::constants::TileSize, "glue terrain grid must match the terrain");
	static_assert(glue_terrain::PageOriginIndex == terrain::constants::MaxPages / 2, "glue terrain grid must match the terrain");

	namespace
	{
		/// Same margin the collector tests its frusta with.
		constexpr float VisibilityMargin = 1.0f;

		constexpr uint32 OuterVerticesPerPageSide = terrain::constants::OuterVerticesPerPageSide;

		/// Tiles per page side, signed for global tile coordinates.
		constexpr int32 TilesPerPage = static_cast<int32>(terrain::constants::TilesPerPage);

		enum class TileKind : uint8
		{
			None,
			Near,
			Far
		};

		struct TileInfo
		{
			TileKind kind = TileKind::None;
			uint64 holeMap = 0;
		};

		/// Tiles by global tile coordinate (page * 16 + tile).
		using TileMap = std::map<std::pair<int32, int32>, TileInfo>;

		/// Distance from a point to a box (0 inside).
		float DistanceToBounds(const Vector3& point, const AABB& bounds)
		{
			const Vector3 closest(
				std::clamp(point.x, bounds.min.x, bounds.max.x),
				std::clamp(point.y, bounds.min.y, bounds.max.y),
				std::clamp(point.z, bounds.min.z, bounds.max.z));
			return (point - closest).GetLength();
		}

		/// The tangent frame terrain::Tile gives a vertex.
		void TileTangentFrame(const Vector3& normal, Vector3& tangent, Vector3& binormal)
		{
			const Vector3 arbitrary = std::abs(normal.y) < 0.99f ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
			tangent = (arbitrary - normal * normal.Dot(arbitrary)).NormalizedCopy();
			binormal = normal.Cross(tangent).NormalizedCopy();
		}

		/// A far mesh vertex: heights and normals from the page as terrain::Tile reads them, white vertex
		/// colour (the baked albedo already holds the vertex shading, like FarTerrain), page-wide texture
		/// coordinates matching the top-down albedo render.
		WorldModelMergeVertex MakeFarVertex(const terrain::Page& page, const int32 pageX, const int32 pageZ, const GlueTerrainGridVertex& grid)
		{
			WorldModelMergeVertex vertex{};

			float height;
			Vector3 normal;
			if (grid.inner)
			{
				height = page.GetInnerHeightAt(grid.x, grid.z);
				normal = ((page.GetNormalAt(grid.x, grid.z) + page.GetNormalAt(grid.x + 1, grid.z)
					+ page.GetNormalAt(grid.x, grid.z + 1) + page.GetNormalAt(grid.x + 1, grid.z + 1)) * 0.25f).NormalizedCopy();
			}
			else
			{
				height = page.GetHeightAt(grid.x, grid.z);
				normal = page.GetNormalAt(grid.x, grid.z);
			}

			vertex.pos = GlueTerrainVertexPosition(pageX, pageZ, grid, height);
			vertex.color = 0xffffffff;
			vertex.normal = normal;
			TileTangentFrame(normal, vertex.tangent, vertex.binormal);

			const float offset = grid.inner ? 0.5f : 0.0f;
			constexpr float cells = static_cast<float>(OuterVerticesPerPageSide - 1);
			vertex.uv[0] = (static_cast<float>(grid.x) + offset) / cells;
			vertex.uv[1] = (static_cast<float>(grid.z) + offset) / cells;
			return vertex;
		}

		/// Serializes into memory with a seekable stream (texture headers are patched at the end).
		template <typename Fn>
		String WriteToBytes(Fn&& write)
		{
			std::ostringstream buffer(std::ios::out | std::ios::binary);
			if (!write(static_cast<std::ostream&>(buffer)))
			{
				return String();
			}

			return buffer.str();
		}

		/// GPU bytes of a DXT1 texture with its mip chain down to 4x4.
		uint64 Dxt1Bytes(uint32 size)
		{
			uint64 bytes = 0;
			for (; size >= 4; size /= 2)
			{
				bytes += static_cast<uint64>(size) * size / 2;
			}
			return bytes;
		}

		class TerrainBakeWriter final
		{
		public:
			TerrainBakeWriter(const String& outDir, GlueTerrainBakeStats& stats)
				: m_outDir(outDir)
				, m_stats(stats)
			{
			}

			/// Writes bytes, counting them. False on failure.
			bool Write(const String& path, const String& bytes)
			{
				const size_t written = bytes.empty() ? 0 : WriteGlueBakeFile(path, bytes);
				if (written == 0)
				{
					ELOG("Glue terrain bake: failed to write " << path);
					++m_stats.failedFiles;
					return false;
				}

				m_stats.fileBytes += written;
				++m_stats.files;
				return true;
			}

			bool WriteMesh(const String& path, const MaterialPtr& material, std::vector<WorldModelMergeVertex> vertices, std::vector<uint32> indices)
			{
				MeshMergeBuckets buckets;
				MeshMergeBucket& bucket = buckets[material ? String(material->GetName()) : String()];
				bucket.material = material;
				bucket.vertices = std::move(vertices);
				bucket.indices = std::move(indices);

				const MeshPtr mesh = CreateMergedMesh(path, buckets);
				if (!mesh)
				{
					return false;
				}

				const size_t written = WriteGlueMeshFile(mesh, path);
				if (written == 0)
				{
					ELOG("Glue terrain bake: failed to write " << path);
					++m_stats.failedFiles;
					return false;
				}

				m_stats.fileBytes += written;
				++m_stats.files;
				m_stats.vertices += buckets.begin()->second.vertices.size();
				return true;
			}

			String Path(const String& file) const
			{
				return JoinGlueBakePath(m_outDir, file);
			}

		private:
			String m_outDir;
			GlueTerrainBakeStats& m_stats;
		};

		/// Writes a near tile's mesh and splatting map. Returns false when nothing was written.
		bool BakeNearTile(TerrainBakeWriter& writer, const terrain::Page& page, const int32 pageX, const int32 pageZ, const uint32 tileX, const uint32 tileZ,
			const uint64 holeMap, GlueTerrainBakeStats& stats, GlueBakeTerrainTile& out)
		{
			const GlueTerrainTileGeometry geometry = BuildGlueNearTileGeometry(tileX, tileZ, holeMap);

#ifdef _DEBUG
			{
				// The near tile must be the live tile: same triangles in the same order.
				std::vector<uint16> live;
				terrain::Tile::GenerateTileIndices(0, 0, 0, 0, 0, holeMap, live);
				ASSERT(live.size() == geometry.indices.size());
				ASSERT(std::equal(live.begin(), live.end(), geometry.indices.begin(), [](const uint16 a, const uint32 b) { return a == b; }));
			}
#endif

			// The live tile's vertices, in its order, moved into world space.
			std::vector<terrain::TileVertex> tileVertices(terrain::constants::VerticesPerTile);
			float minHeight = 0.0f;
			float maxHeight = 0.0f;
			terrain::Tile::FillTileVertices(page, tileX, tileZ, 1.0f, 0.0f, 0.0f, tileVertices.data(), minHeight, maxHeight);
			ASSERT(geometry.vertices.size() == tileVertices.size());

			std::vector<WorldModelMergeVertex> vertices(geometry.vertices.size());
			for (size_t i = 0; i < vertices.size(); ++i)
			{
				const terrain::TileVertex& source = tileVertices[i];
				WorldModelMergeVertex& target = vertices[i];
				target.pos = GlueTerrainVertexPosition(pageX, pageZ, geometry.vertices[i], source.position.y);
				target.color = source.color;
				target.normal = source.normal;
				target.binormal = source.binormal;
				target.tangent = source.tangent;
				target.uv[0] = source.u;
				target.uv[1] = source.v;
			}

			const MaterialPtr material = page.GetTileMaterial(tileX, tileZ);
			if (!material)
			{
				ELOG("Glue terrain bake: tile " << tileX << "," << tileZ << " of page " << pageX << "," << pageZ << " has no material");
				return false;
			}

			const String suffix = std::to_string(pageX) + "_" + std::to_string(pageZ) + "_" + std::to_string(tileX) + "_" + std::to_string(tileZ);
			const String meshPath = writer.Path("NearTile_" + suffix + ".hmsh");
			const String splatPath = writer.Path("Splat_" + suffix + ".htex");

			// The coverage texels exactly as terrain::Tile uploads them.
			constexpr uint32 pixels = terrain::constants::PixelsPerTile;
			std::vector<uint32> coverage(pixels * pixels);
			const size_t pixelStartX = tileX * (pixels - 1);
			const size_t pixelStartY = tileZ * (pixels - 1);
			for (size_t localX = 0; localX < pixels; ++localX)
			{
				for (size_t localY = 0; localY < pixels; ++localY)
				{
					coverage[localY + localX * pixels] = page.GetLayersAt(pixelStartX + localX, pixelStartY + localY);
				}
			}

			const String splatBytes = WriteToBytes([&](std::ostream& stream) { return page_bake::WriteRgba8Texture(stream, coverage.data(), pixels, pixels); });
			if (!writer.Write(splatPath, splatBytes))
			{
				return false;
			}
			stats.textureBytes += static_cast<uint64>(pixels) * pixels * 4;

			const size_t triangles = geometry.indices.size() / 3;
			if (!writer.WriteMesh(meshPath, material, std::move(vertices), geometry.indices))
			{
				return false;
			}

			stats.nearTriangles += triangles;
			out.mesh = meshPath;
			out.material = material->GetName();
			out.splatting = splatPath;
			return true;
		}
	}

	std::vector<GlueBakeTerrainTile> BakeGlueTerrain(WorldEditorInstance& world, const std::vector<PagePosition>& pages, const GlueBakeFrustumSet& frusta,
		const std::vector<GlueCameraPose>& poses, const String& outDir, const float nearDistance, GlueTerrainBakeStats* stats)
	{
		GlueTerrainBakeStats localStats;
		GlueTerrainBakeStats& counts = stats ? *stats : localStats;
		counts = GlueTerrainBakeStats{};

		std::vector<GlueBakeTerrainTile> nearResult;
		std::vector<GlueBakeTerrainTile> farResult;
		TerrainBakeWriter writer(outDir, counts);

		// Classify every tile first: far tiles need to know which neighbours are drawn at full resolution.
		TileMap tiles;
		std::vector<PagePosition> visiblePages;
		for (const PagePosition& position : pages)
		{
			const terrain::Page* page = world.GetPreparedTerrainPage(position);
			if (!page)
			{
				continue;
			}

			const int32 pageX = static_cast<int32>(position.x());
			const int32 pageZ = static_cast<int32>(position.y());
			bool anyVisible = false;

			for (uint32 tileZ = 0; tileZ < terrain::constants::TilesPerPage; ++tileZ)
			{
				for (uint32 tileX = 0; tileX < terrain::constants::TilesPerPage; ++tileX)
				{
					std::vector<terrain::TileVertex> vertices(terrain::constants::VerticesPerTile);
					float minHeight = 0.0f;
					float maxHeight = 0.0f;
					terrain::Tile::FillTileVertices(*page, tileX, tileZ, 1.0f, 0.0f, 0.0f, vertices.data(), minHeight, maxHeight);

					const uint32 startX = tileX * glue_terrain::CellsPerTileSide;
					const uint32 startZ = tileZ * glue_terrain::CellsPerTileSide;
					const Vector3 minCorner = GlueTerrainVertexPosition(pageX, pageZ, GlueTerrainGridVertex{ startX, startZ, false }, minHeight);
					const Vector3 maxCorner = GlueTerrainVertexPosition(pageX, pageZ, GlueTerrainGridVertex{ startX + glue_terrain::CellsPerTileSide, startZ + glue_terrain::CellsPerTileSide, false }, maxHeight);
					const AABB bounds(minCorner, maxCorner);

					if (!std::isfinite(minHeight) || !std::isfinite(maxHeight) || !frusta.Intersects(bounds, VisibilityMargin))
					{
						continue;
					}

					const uint64 holeMap = page->GetTileHoleMap(tileX, tileZ);
					if (holeMap == ~uint64{ 0 })
					{
						++counts.emptyTiles;
						continue;
					}

					bool isNear = false;
					for (const GlueCameraPose& pose : poses)
					{
						if (DistanceToBounds(pose.position, bounds) <= nearDistance)
						{
							isNear = true;
							break;
						}
					}

					TileInfo& info = tiles[{ pageX * TilesPerPage + static_cast<int32>(tileX), pageZ * TilesPerPage + static_cast<int32>(tileZ) }];
					info.kind = isNear ? TileKind::Near : TileKind::Far;
					info.holeMap = holeMap;
					anyVisible = true;
				}
			}

			if (anyVisible)
			{
				visiblePages.push_back(position);
			}
		}

		counts.pages = static_cast<uint32>(visiblePages.size());

		for (const PagePosition& position : visiblePages)
		{
			const int32 pageX = static_cast<int32>(position.x());
			const int32 pageZ = static_cast<int32>(position.y());

			// Read the page's far colour before taking the page data: the render loads and may release the page.
			bool pageHasFar = false;
			for (uint32 tileZ = 0; tileZ < terrain::constants::TilesPerPage && !pageHasFar; ++tileZ)
			{
				for (uint32 tileX = 0; tileX < terrain::constants::TilesPerPage && !pageHasFar; ++tileX)
				{
					const auto it = tiles.find({ pageX * TilesPerPage + static_cast<int32>(tileX), pageZ * TilesPerPage + static_cast<int32>(tileZ) });
					pageHasFar = it != tiles.end() && it->second.kind == TileKind::Far;
				}
			}

			std::vector<uint8> albedo;
			if (pageHasFar && !world.RenderTerrainPageColor(position, GlueFarAlbedoSize, albedo))
			{
				ELOG("Glue terrain bake: failed to render the far colour of page " << pageX << "," << pageZ);
				++counts.failedFiles;
				pageHasFar = false;
			}

			const terrain::Page* page = world.GetPreparedTerrainPage(position);
			if (!page)
			{
				continue;
			}

			// Far tiles of this page, merged into one mesh
			std::map<GlueTerrainGridVertex, uint32> farVertexIndices;
			std::vector<WorldModelMergeVertex> farVertices;
			std::vector<uint32> farIndices;
			uint32 farTiles = 0;

			for (uint32 tileZ = 0; tileZ < terrain::constants::TilesPerPage; ++tileZ)
			{
				for (uint32 tileX = 0; tileX < terrain::constants::TilesPerPage; ++tileX)
				{
					const int32 globalX = pageX * TilesPerPage + static_cast<int32>(tileX);
					const int32 globalZ = pageZ * TilesPerPage + static_cast<int32>(tileZ);
					const auto it = tiles.find({ globalX, globalZ });
					if (it == tiles.end())
					{
						continue;
					}

					const TileInfo& info = it->second;
					if (info.kind == TileKind::Near)
					{
						GlueBakeTerrainTile entry;
						if (BakeNearTile(writer, *page, pageX, pageZ, tileX, tileZ, info.holeMap, counts, entry))
						{
							nearResult.push_back(std::move(entry));
							++counts.nearTiles;
						}
						continue;
					}

					if (!pageHasFar)
					{
						continue;
					}

					// A neighbour block is drawn at full resolution when it lies in a near tile or is a far
					// block with a hole.
					const auto isFineNeighbourBlock = [&](const int32 blockX, const int32 blockZ)
					{
						constexpr int32 blocks = static_cast<int32>(glue_terrain::FarBlocksPerTileSide);
						int32 neighbourX = globalX;
						int32 neighbourZ = globalZ;
						int32 neighbourBlockX = blockX;
						int32 neighbourBlockZ = blockZ;
						if (blockX < 0)
						{
							--neighbourX;
							neighbourBlockX += blocks;
						}
						else if (blockX >= blocks)
						{
							++neighbourX;
							neighbourBlockX -= blocks;
						}

						if (blockZ < 0)
						{
							--neighbourZ;
							neighbourBlockZ += blocks;
						}
						else if (blockZ >= blocks)
						{
							++neighbourZ;
							neighbourBlockZ -= blocks;
						}

						const auto neighbour = tiles.find({ neighbourX, neighbourZ });
						if (neighbour == tiles.end())
						{
							return false;
						}

						if (neighbour->second.kind == TileKind::Near)
						{
							return true;
						}

						return neighbour->second.kind == TileKind::Far &&
							GlueFarBlockHasHole(neighbour->second.holeMap, static_cast<uint32>(neighbourBlockX), static_cast<uint32>(neighbourBlockZ));
					};

					const GlueTerrainTileGeometry geometry = BuildGlueFarTileGeometry(tileX, tileZ, info.holeMap, isFineNeighbourBlock);
					for (const uint32 index : geometry.indices)
					{
						const GlueTerrainGridVertex& grid = geometry.vertices[index];
						auto [vertexIt, inserted] = farVertexIndices.emplace(grid, static_cast<uint32>(farVertices.size()));
						if (inserted)
						{
							farVertices.push_back(MakeFarVertex(*page, pageX, pageZ, grid));
						}
						farIndices.push_back(vertexIt->second);
					}

					++farTiles;
				}
			}

			if (farIndices.empty())
			{
				continue;
			}

			const String suffix = std::to_string(pageX) + "_" + std::to_string(pageZ);
			const String albedoPath = writer.Path("FarAlbedo_" + suffix + ".htex");
			const String materialPath = writer.Path("FarAlbedo_" + suffix + ".hmi");
			const String meshPath = writer.Path("FarTile_" + suffix + ".hmsh");

			const String albedoBytes = WriteToBytes([&](std::ostream& stream) { return page_bake::WriteDxt1Texture(stream, std::move(albedo), GlueFarAlbedoSize); });
			if (!writer.Write(albedoPath, albedoBytes))
			{
				continue;
			}
			counts.textureBytes += Dxt1Bytes(GlueFarAlbedoSize);

			// Set up like FarTerrain's page material: the far terrain base with the page colour as "Albedo".
			const MaterialPtr parent = MaterialManager::Get().Load(GlueFarTerrainMaterial);
			if (!parent)
			{
				ELOG("Glue terrain bake: cannot load " << GlueFarTerrainMaterial);
				++counts.failedFiles;
				continue;
			}

			const auto material = std::make_shared<MaterialInstance>(materialPath, parent);
			material->SetTextureParameter("Albedo", albedoPath);
			const String materialBytes = WriteToBytes([&](std::ostream& stream)
			{
				io::StreamSink sink{ stream };
				io::Writer materialWriter{ sink };
				MaterialInstanceSerializer serializer;
				serializer.Export(*material, materialWriter);
				return true;
			});
			if (!writer.Write(materialPath, materialBytes))
			{
				continue;
			}

			const size_t triangles = farIndices.size() / 3;
			if (!writer.WriteMesh(meshPath, material, std::move(farVertices), std::move(farIndices)))
			{
				continue;
			}

			counts.farTriangles += triangles;
			counts.farTiles += farTiles;
			++counts.farPages;
			farResult.push_back(GlueBakeTerrainTile{ meshPath, materialPath, String() });
		}

		nearResult.insert(nearResult.end(), farResult.begin(), farResult.end());
		return nearResult;
	}
}
