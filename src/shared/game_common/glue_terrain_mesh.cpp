// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "glue_terrain_mesh.h"

#include <algorithm>
#include <map>
#include <set>

namespace mmo
{
	namespace
	{
		/// Collects the vertices of a tile geometry, each grid vertex once.
		class VertexCollector final
		{
		public:
			explicit VertexCollector(GlueTerrainTileGeometry& geometry)
				: m_geometry(geometry)
			{
			}

			uint32 Get(const GlueTerrainGridVertex& vertex)
			{
				const auto it = m_indices.find(vertex);
				if (it != m_indices.end())
				{
					return it->second;
				}

				const uint32 index = static_cast<uint32>(m_geometry.vertices.size());
				m_geometry.vertices.push_back(vertex);
				m_indices.emplace(vertex, index);
				return index;
			}

			void Triangle(const GlueTerrainGridVertex& a, const GlueTerrainGridVertex& b, const GlueTerrainGridVertex& c)
			{
				m_geometry.indices.push_back(Get(a));
				m_geometry.indices.push_back(Get(b));
				m_geometry.indices.push_back(Get(c));
			}

		private:
			GlueTerrainTileGeometry& m_geometry;
			std::map<GlueTerrainGridVertex, uint32> m_indices;
		};

		/// The four triangles of a full resolution cell around its inner vertex, as terrain tiles draw it.
		/// cellX/cellZ are page-local cell (inner vertex) indices.
		void AddFineCell(VertexCollector& collector, const uint32 cellX, const uint32 cellZ)
		{
			const GlueTerrainGridVertex inner{ cellX, cellZ, true };
			const GlueTerrainGridVertex topLeft{ cellX, cellZ, false };
			const GlueTerrainGridVertex topRight{ cellX + 1, cellZ, false };
			const GlueTerrainGridVertex bottomLeft{ cellX, cellZ + 1, false };
			const GlueTerrainGridVertex bottomRight{ cellX + 1, cellZ + 1, false };

			collector.Triangle(inner, topRight, topLeft);
			collector.Triangle(inner, bottomRight, topRight);
			collector.Triangle(inner, bottomLeft, bottomRight);
			collector.Triangle(inner, topLeft, bottomLeft);
		}
	}

	Vector3 GlueTerrainVertexPosition(const int32 pageX, const int32 pageZ, const GlueTerrainGridVertex& vertex, const float height)
	{
		// Global grid coordinate in half cells: outer vertices sit on even, inner vertices on odd values.
		constexpr int64 cellsPerPage = static_cast<int64>(glue_terrain::CellsPerTileSide) * glue_terrain::TilesPerPage;
		const int64 halfX = (static_cast<int64>(pageX - glue_terrain::PageOriginIndex) * cellsPerPage + vertex.x) * 2 + (vertex.inner ? 1 : 0);
		const int64 halfZ = (static_cast<int64>(pageZ - glue_terrain::PageOriginIndex) * cellsPerPage + vertex.z) * 2 + (vertex.inner ? 1 : 0);

		constexpr double halfCell = glue_terrain::TileSize / static_cast<double>(glue_terrain::CellsPerTileSide * 2);
		return Vector3(static_cast<float>(static_cast<double>(halfX) * halfCell), height, static_cast<float>(static_cast<double>(halfZ) * halfCell));
	}

	bool IsGlueTerrainHole(const uint64 holeMap, const uint32 cellX, const uint32 cellZ)
	{
		if (cellX >= glue_terrain::InnerVerticesPerTileSide || cellZ >= glue_terrain::InnerVerticesPerTileSide)
		{
			return false;
		}

		const uint32 bit = cellX + cellZ * glue_terrain::InnerVerticesPerTileSide;
		return (holeMap & (uint64{ 1 } << bit)) != 0;
	}

	bool GlueFarBlockHasHole(const uint64 holeMap, const uint32 blockX, const uint32 blockZ)
	{
		for (uint32 dz = 0; dz < glue_terrain::FarBlockCells; ++dz)
		{
			for (uint32 dx = 0; dx < glue_terrain::FarBlockCells; ++dx)
			{
				if (IsGlueTerrainHole(holeMap, blockX * glue_terrain::FarBlockCells + dx, blockZ * glue_terrain::FarBlockCells + dz))
				{
					return true;
				}
			}
		}

		return false;
	}

	GlueTerrainTileGeometry BuildGlueNearTileGeometry(const uint32 tileX, const uint32 tileZ, const uint64 holeMap)
	{
		GlueTerrainTileGeometry geometry;

		const uint32 startX = tileX * glue_terrain::CellsPerTileSide;
		const uint32 startZ = tileZ * glue_terrain::CellsPerTileSide;

		// Vertex order of terrain::Tile: outer vertices row by row, then the inner vertices.
		geometry.vertices.reserve(glue_terrain::OuterVerticesPerTileSide * glue_terrain::OuterVerticesPerTileSide
			+ glue_terrain::InnerVerticesPerTileSide * glue_terrain::InnerVerticesPerTileSide);
		for (uint32 j = 0; j < glue_terrain::OuterVerticesPerTileSide; ++j)
		{
			for (uint32 i = 0; i < glue_terrain::OuterVerticesPerTileSide; ++i)
			{
				geometry.vertices.push_back(GlueTerrainGridVertex{ startX + i, startZ + j, false });
			}
		}

		for (uint32 j = 0; j < glue_terrain::InnerVerticesPerTileSide; ++j)
		{
			for (uint32 i = 0; i < glue_terrain::InnerVerticesPerTileSide; ++i)
			{
				geometry.vertices.push_back(GlueTerrainGridVertex{ startX + i, startZ + j, true });
			}
		}

		constexpr uint32 outerCount = glue_terrain::OuterVerticesPerTileSide * glue_terrain::OuterVerticesPerTileSide;
		const auto outer = [](const uint32 x, const uint32 z) { return x + z * glue_terrain::OuterVerticesPerTileSide; };

		geometry.indices.reserve(glue_terrain::InnerVerticesPerTileSide * glue_terrain::InnerVerticesPerTileSide * 12);
		for (uint32 j = 0; j < glue_terrain::InnerVerticesPerTileSide; ++j)
		{
			for (uint32 i = 0; i < glue_terrain::InnerVerticesPerTileSide; ++i)
			{
				if (IsGlueTerrainHole(holeMap, i, j))
				{
					continue;
				}

				const uint32 inner = outerCount + i + j * glue_terrain::InnerVerticesPerTileSide;
				const uint32 topLeft = outer(i, j);
				const uint32 topRight = outer(i + 1, j);
				const uint32 bottomLeft = outer(i, j + 1);
				const uint32 bottomRight = outer(i + 1, j + 1);

				geometry.indices.insert(geometry.indices.end(), { inner, topRight, topLeft });
				geometry.indices.insert(geometry.indices.end(), { inner, bottomRight, topRight });
				geometry.indices.insert(geometry.indices.end(), { inner, bottomLeft, bottomRight });
				geometry.indices.insert(geometry.indices.end(), { inner, topLeft, bottomLeft });
			}
		}

		return geometry;
	}

	GlueTerrainTileGeometry BuildGlueFarTileGeometry(const uint32 tileX, const uint32 tileZ, const uint64 holeMap, const std::function<bool(int32 blockX, int32 blockZ)>& isFineNeighbourBlock)
	{
		GlueTerrainTileGeometry geometry;
		VertexCollector collector(geometry);

		const uint32 startX = tileX * glue_terrain::CellsPerTileSide;
		const uint32 startZ = tileZ * glue_terrain::CellsPerTileSide;
		constexpr int32 blocks = static_cast<int32>(glue_terrain::FarBlocksPerTileSide);

		const auto isFine = [&](const int32 blockX, const int32 blockZ)
		{
			if (blockX >= 0 && blockZ >= 0 && blockX < blocks && blockZ < blocks)
			{
				return GlueFarBlockHasHole(holeMap, static_cast<uint32>(blockX), static_cast<uint32>(blockZ));
			}

			return isFineNeighbourBlock && isFineNeighbourBlock(blockX, blockZ);
		};

		for (int32 blockZ = 0; blockZ < blocks; ++blockZ)
		{
			for (int32 blockX = 0; blockX < blocks; ++blockX)
			{
				const uint32 cellX = startX + static_cast<uint32>(blockX) * glue_terrain::FarBlockCells;
				const uint32 cellZ = startZ + static_cast<uint32>(blockZ) * glue_terrain::FarBlockCells;
				const uint32 localCellX = static_cast<uint32>(blockX) * glue_terrain::FarBlockCells;
				const uint32 localCellZ = static_cast<uint32>(blockZ) * glue_terrain::FarBlockCells;

				if (GlueFarBlockHasHole(holeMap, static_cast<uint32>(blockX), static_cast<uint32>(blockZ)))
				{
					// Full resolution, so the hole keeps its exact outline.
					for (uint32 dz = 0; dz < glue_terrain::FarBlockCells; ++dz)
					{
						for (uint32 dx = 0; dx < glue_terrain::FarBlockCells; ++dx)
						{
							if (!IsGlueTerrainHole(holeMap, localCellX + dx, localCellZ + dz))
							{
								AddFineCell(collector, cellX + dx, cellZ + dz);
							}
						}
					}

					continue;
				}

				// Walk the block's border from its top left corner along +X, +Z, -X, -Z, taking each side's
				// middle vertex where the neighbour across it is drawn at full resolution.
				std::vector<GlueTerrainGridVertex> ring;
				ring.reserve(8);
				ring.push_back({ cellX, cellZ, false });
				if (isFine(blockX, blockZ - 1))
				{
					ring.push_back({ cellX + 1, cellZ, false });
				}
				ring.push_back({ cellX + 2, cellZ, false });
				if (isFine(blockX + 1, blockZ))
				{
					ring.push_back({ cellX + 2, cellZ + 1, false });
				}
				ring.push_back({ cellX + 2, cellZ + 2, false });
				if (isFine(blockX, blockZ + 1))
				{
					ring.push_back({ cellX + 1, cellZ + 2, false });
				}
				ring.push_back({ cellX, cellZ + 2, false });
				if (isFine(blockX - 1, blockZ))
				{
					ring.push_back({ cellX, cellZ + 1, false });
				}

				const GlueTerrainGridVertex centre{ cellX + 1, cellZ + 1, false };
				for (size_t k = 0; k < ring.size(); ++k)
				{
					collector.Triangle(centre, ring[(k + 1) % ring.size()], ring[k]);
				}
			}
		}

		return geometry;
	}

	std::vector<Vector3> GlueTerrainEdgeVertices(const GlueTerrainTileGeometry& geometry, const uint32 tileX, const uint32 tileZ, const GlueTerrainSide side,
		const int32 pageX, const int32 pageZ, const std::function<float(const GlueTerrainGridVertex&)>& height)
	{
		const uint32 minX = tileX * glue_terrain::CellsPerTileSide;
		const uint32 minZ = tileZ * glue_terrain::CellsPerTileSide;
		const uint32 maxX = minX + glue_terrain::CellsPerTileSide;
		const uint32 maxZ = minZ + glue_terrain::CellsPerTileSide;

		std::set<uint32> used(geometry.indices.begin(), geometry.indices.end());

		std::vector<Vector3> result;
		for (const uint32 index : used)
		{
			if (index >= geometry.vertices.size())
			{
				continue;
			}

			const GlueTerrainGridVertex& vertex = geometry.vertices[index];
			if (vertex.inner)
			{
				continue;
			}

			bool onSide = false;
			switch (side)
			{
			case GlueTerrainSide::North:
				onSide = vertex.z == minZ;
				break;
			case GlueTerrainSide::East:
				onSide = vertex.x == maxX;
				break;
			case GlueTerrainSide::South:
				onSide = vertex.z == maxZ;
				break;
			case GlueTerrainSide::West:
				onSide = vertex.x == minX;
				break;
			}

			if (onSide)
			{
				result.push_back(GlueTerrainVertexPosition(pageX, pageZ, vertex, height ? height(vertex) : 0.0f));
			}
		}

		const bool alongX = side == GlueTerrainSide::North || side == GlueTerrainSide::South;
		std::sort(result.begin(), result.end(), [alongX](const Vector3& a, const Vector3& b)
		{
			return alongX ? a.x < b.x : a.z < b.z;
		});

		return result;
	}
}
