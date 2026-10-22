// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

#include <functional>
#include <vector>

namespace mmo
{
	/// @brief Terrain grid constants the glue terrain bake builds its meshes on.
	/// @remark Mirrors terrain::constants (game_common does not link the terrain library); the editor's
	///         terrain baker static_asserts that they match.
	namespace glue_terrain
	{
		/// @brief Outer (corner) vertices along one tile side.
		constexpr uint32 OuterVerticesPerTileSide = 9;

		/// @brief Inner (cell centre) vertices along one tile side, one per cell.
		constexpr uint32 InnerVerticesPerTileSide = 8;

		/// @brief Cells along one tile side.
		constexpr uint32 CellsPerTileSide = OuterVerticesPerTileSide - 1;

		/// @brief Tiles along one page side.
		constexpr uint32 TilesPerPage = 16;

		/// @brief Page index whose minimum corner is the world origin (terrain::Page places page x at (x - 32) * PageSize).
		constexpr int32 PageOriginIndex = 32;

		/// @brief Edge length of a tile in world units (the same float literal terrain::constants uses).
		constexpr double TileSize = 33.33333f;

		/// @brief Cells along one side of a far tile block. Far tiles are decimated to stride 2: each
		///        2x2 cell block becomes a fan around its centre outer vertex.
		constexpr uint32 FarBlockCells = 2;

		/// @brief Blocks along one far tile side.
		constexpr uint32 FarBlocksPerTileSide = CellsPerTileSide / FarBlockCells;
	}

	/// @brief One vertex of the terrain grid of a page, by page-local grid coordinate.
	struct GlueTerrainGridVertex
	{
		/// @brief Page-local X index: 0..128 for outer vertices, 0..127 for inner vertices.
		uint32 x = 0;

		/// @brief Page-local Z index, like x.
		uint32 z = 0;

		/// @brief Whether this is an inner (cell centre) vertex rather than an outer (corner) vertex.
		bool inner = false;

		[[nodiscard]] bool operator<(const GlueTerrainGridVertex& other) const
		{
			if (inner != other.inner)
			{
				return !inner;
			}

			if (z != other.z)
			{
				return z < other.z;
			}

			return x < other.x;
		}

		[[nodiscard]] bool operator==(const GlueTerrainGridVertex& other) const
		{
			return x == other.x && z == other.z && inner == other.inner;
		}
	};

	/// @brief Side of a tile. North is the tile's minimum Z side, west its minimum X side.
	enum class GlueTerrainSide : uint8
	{
		North,
		East,
		South,
		West
	};

	/// @brief Geometry of one baked terrain tile, by grid vertex: positions, normals and texture
	///        coordinates are filled in from the page by the caller.
	struct GlueTerrainTileGeometry
	{
		std::vector<GlueTerrainGridVertex> vertices;

		/// @brief Triangle list into vertices, wound like terrain tiles (counter-clockwise seen from above).
		std::vector<uint32> indices;
	};

	/// @brief World position of a terrain grid vertex.
	/// @details Computed from the vertex' global grid coordinate in double precision, so a vertex shared
	///          by two tiles or two pages gets bit-identical positions whichever side builds it.
	/// @param pageX Page X index in the world's page grid.
	/// @param pageZ Page Z index in the world's page grid.
	/// @param vertex The page-local grid vertex.
	/// @param height The vertex height.
	[[nodiscard]] Vector3 GlueTerrainVertexPosition(int32 pageX, int32 pageZ, const GlueTerrainGridVertex& vertex, float height);

	/// @brief Whether a cell (inner vertex) of a tile is a hole.
	/// @param holeMap The tile's hole map: bit (cellX + cellZ * 8) set marks a hole.
	[[nodiscard]] bool IsGlueTerrainHole(uint64 holeMap, uint32 cellX, uint32 cellZ);

	/// @brief Whether a far tile block (2x2 cells) contains a hole. Such blocks are kept at full
	///        resolution, so the hole keeps its exact shape.
	[[nodiscard]] bool GlueFarBlockHasHole(uint64 holeMap, uint32 blockX, uint32 blockZ);

	/// @brief Full resolution geometry of a near tile, identical to a live terrain tile at LOD 0.
	/// @details The 81 outer vertices (row by row), then the 64 inner vertices, in terrain::Tile's order;
	///          every non-hole cell becomes four triangles around its inner vertex (top, right, bottom,
	///          left), as terrain::Tile::GenerateTileIndices emits them for LOD 0 without stitching.
	/// @param tileX Tile X index within its page (0..15).
	/// @param tileZ Tile Z index within its page (0..15).
	/// @param holeMap The tile's hole map.
	[[nodiscard]] GlueTerrainTileGeometry BuildGlueNearTileGeometry(uint32 tileX, uint32 tileZ, uint64 holeMap);

	/// @brief Decimated geometry of a far tile.
	/// @details Each 2x2 cell block becomes a fan around its centre outer vertex over its corners. A side
	///          that borders a full resolution block (a near tile, or a far block with a hole) also takes
	///          that side's middle vertex, so the shared edge has exactly the vertices of the full
	///          resolution side and no crack opens. Blocks containing a hole are emitted at full resolution
	///          like a near tile, without their hole cells. Only vertices used by a triangle are listed.
	/// @param tileX Tile X index within its page (0..15).
	/// @param tileZ Tile Z index within its page (0..15).
	/// @param holeMap The tile's hole map.
	/// @param isFineNeighbourBlock Called for blocks just outside the tile (tile-local block coordinates -1
	///        or FarBlocksPerTileSide on one axis): whether that block is drawn at full resolution. Blocks
	///        nobody draws count as coarse.
	[[nodiscard]] GlueTerrainTileGeometry BuildGlueFarTileGeometry(uint32 tileX, uint32 tileZ, uint64 holeMap, const std::function<bool(int32 blockX, int32 blockZ)>& isFineNeighbourBlock);

	/// @brief World positions of a tile geometry's vertices that lie on one side of the tile and are used
	///        by a triangle, sorted along the side (by X for north/south, by Z for east/west).
	/// @param geometry The tile geometry.
	/// @param tileX Tile X index within its page.
	/// @param tileZ Tile Z index within its page.
	/// @param side The side.
	/// @param pageX Page X index.
	/// @param pageZ Page Z index.
	/// @param height Height of a grid vertex.
	[[nodiscard]] std::vector<Vector3> GlueTerrainEdgeVertices(const GlueTerrainTileGeometry& geometry, uint32 tileX, uint32 tileZ, GlueTerrainSide side,
		int32 pageX, int32 pageZ, const std::function<float(const GlueTerrainGridVertex&)>& height);
}
