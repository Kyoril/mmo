// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_common/glue_terrain_mesh.h"

#include <cmath>
#include <set>
#include <utility>
#include <vector>

using namespace mmo;

namespace
{
	constexpr int32 PageX = 30;
	constexpr int32 PageZ = 33;

	/// A bumpy height field over global grid coordinates, so a vertex shared by two pages gets the same
	/// height from both, like real terrain pages.
	std::function<float(const GlueTerrainGridVertex&)> heightsOfPage(const int32 pageX, const int32 pageZ)
	{
		return [pageX, pageZ](const GlueTerrainGridVertex& v)
		{
			const double x = (pageX * 128.0 + v.x) + (v.inner ? 0.5 : 0.0);
			const double z = (pageZ * 128.0 + v.z) + (v.inner ? 0.5 : 0.0);
			return static_cast<float>(std::sin(x * 0.37) * 3.0 + std::cos(z * 0.21) * 2.0 + 17.0);
		};
	}

	bool sameEdge(const std::vector<Vector3>& a, const std::vector<Vector3>& b)
	{
		if (a.size() != b.size())
		{
			return false;
		}

		for (size_t i = 0; i < a.size(); ++i)
		{
			// Bitwise equal: anything else would leave a crack, however thin.
			if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].z != b[i].z)
			{
				return false;
			}
		}

		return true;
	}

	/// Grid position in half cells (outer vertices even, inner vertices odd).
	std::pair<int64, int64> halfCoord(const GlueTerrainGridVertex& v)
	{
		return { static_cast<int64>(v.x) * 2 + (v.inner ? 1 : 0), static_cast<int64>(v.z) * 2 + (v.inner ? 1 : 0) };
	}

	/// Whether a used vertex lies strictly inside some triangle edge: a T-junction, which cracks.
	bool hasTJunction(const GlueTerrainTileGeometry& geometry)
	{
		std::set<std::pair<int64, int64>> used;
		for (const uint32 index : geometry.indices)
		{
			used.insert(halfCoord(geometry.vertices[index]));
		}

		for (size_t t = 0; t + 2 < geometry.indices.size(); t += 3)
		{
			for (int e = 0; e < 3; ++e)
			{
				const auto a = halfCoord(geometry.vertices[geometry.indices[t + e]]);
				const auto b = halfCoord(geometry.vertices[geometry.indices[t + (e + 1) % 3]]);
				for (const auto& p : used)
				{
					if (p == a || p == b)
					{
						continue;
					}

					const int64 cross = (b.first - a.first) * (p.second - a.second) - (b.second - a.second) * (p.first - a.first);
					if (cross != 0)
					{
						continue;
					}

					const int64 dot = (p.first - a.first) * (b.first - a.first) + (p.second - a.second) * (b.second - a.second);
					const int64 lengthSquared = (b.first - a.first) * (b.first - a.first) + (b.second - a.second) * (b.second - a.second);
					if (dot > 0 && dot < lengthSquared)
					{
						return true;
					}
				}
			}
		}

		return false;
	}

	/// Whether every triangle faces up on flat ground.
	bool allFaceUp(const GlueTerrainTileGeometry& geometry)
	{
		for (size_t t = 0; t + 2 < geometry.indices.size(); t += 3)
		{
			const Vector3 a = GlueTerrainVertexPosition(PageX, PageZ, geometry.vertices[geometry.indices[t]], 0.0f);
			const Vector3 b = GlueTerrainVertexPosition(PageX, PageZ, geometry.vertices[geometry.indices[t + 1]], 0.0f);
			const Vector3 c = GlueTerrainVertexPosition(PageX, PageZ, geometry.vertices[geometry.indices[t + 2]], 0.0f);
			if ((b - a).Cross(c - a).y <= 0.0f)
			{
				return false;
			}
		}

		return true;
	}

	uint64 holeBit(const uint32 cellX, const uint32 cellZ)
	{
		return uint64{ 1 } << (cellX + cellZ * 8);
	}
}

TEST_CASE("Glue terrain vertex positions follow the terrain page grid", "[glue_terrain]")
{
	// Page 32 starts at the world origin; one cell is TileSize / 8.
	const Vector3 origin = GlueTerrainVertexPosition(32, 32, GlueTerrainGridVertex{ 0, 0, false }, 1.5f);
	CHECK(origin.x == 0.0f);
	CHECK(origin.y == 1.5f);
	CHECK(origin.z == 0.0f);

	const Vector3 inner = GlueTerrainVertexPosition(32, 32, GlueTerrainGridVertex{ 0, 0, true }, 0.0f);
	CHECK(inner.x == Approx(33.33333 / 16.0));
	CHECK(inner.z == Approx(33.33333 / 16.0));

	const Vector3 west = GlueTerrainVertexPosition(30, 33, GlueTerrainGridVertex{ 8, 0, false }, 0.0f);
	CHECK(west.x == Approx(-2.0 * 16.0 * 33.33333 + 33.33333).epsilon(1e-6));
	CHECK(west.z == Approx(16.0 * 33.33333).epsilon(1e-6));

	// The last outer vertex of a page is the first of the next one.
	const Vector3 pageEnd = GlueTerrainVertexPosition(30, 33, GlueTerrainGridVertex{ 128, 5, false }, 0.0f);
	const Vector3 nextStart = GlueTerrainVertexPosition(31, 33, GlueTerrainGridVertex{ 0, 5, false }, 0.0f);
	CHECK(pageEnd.x == nextStart.x);
	CHECK(pageEnd.z == nextStart.z);
}

TEST_CASE("Glue near tiles use the live terrain tile layout", "[glue_terrain]")
{
	const GlueTerrainTileGeometry tile = BuildGlueNearTileGeometry(3, 5, 0);
	REQUIRE(tile.vertices.size() == 145);
	CHECK(tile.indices.size() == 64 * 4 * 3);
	CHECK(tile.vertices[0] == GlueTerrainGridVertex{ 24, 40, false });
	CHECK(tile.vertices[80] == GlueTerrainGridVertex{ 32, 48, false });
	CHECK(tile.vertices[81] == GlueTerrainGridVertex{ 24, 40, true });
	CHECK(tile.vertices[144] == GlueTerrainGridVertex{ 31, 47, true });

	// First cell: top, right, bottom, left triangle around inner vertex 81, like terrain::Tile at LOD 0.
	const std::vector<uint32> firstCell(tile.indices.begin(), tile.indices.begin() + 12);
	CHECK(firstCell == std::vector<uint32>{ 81, 1, 0, 81, 10, 1, 81, 9, 10, 81, 0, 9 });

	CHECK(allFaceUp(tile));
	CHECK_FALSE(hasTJunction(tile));

	const GlueTerrainTileGeometry holed = BuildGlueNearTileGeometry(3, 5, holeBit(2, 6) | holeBit(7, 7));
	CHECK(holed.indices.size() == 62 * 4 * 3);
}

TEST_CASE("Glue far tiles are decimated to stride 2", "[glue_terrain]")
{
	const GlueTerrainTileGeometry tile = BuildGlueFarTileGeometry(4, 5, 0, [](int32, int32) { return false; });

	// 16 blocks of 4 triangles; 5 outer vertices per side plus the 16 block centres.
	CHECK(tile.indices.size() == 16 * 4 * 3);
	CHECK(tile.vertices.size() == 25 + 16);
	CHECK(allFaceUp(tile));
	CHECK_FALSE(hasTJunction(tile));

	const auto heights = heightsOfPage(PageX, PageZ);
	CHECK(GlueTerrainEdgeVertices(tile, 4, 5, GlueTerrainSide::West, PageX, PageZ, heights).size() == 5);
	CHECK(GlueTerrainEdgeVertices(tile, 4, 5, GlueTerrainSide::North, PageX, PageZ, heights).size() == 5);
}

TEST_CASE("Glue near and far terrain edges match exactly on a shared border", "[glue_terrain]")
{
	const auto heights = heightsOfPage(PageX, PageZ);
	const GlueTerrainTileGeometry nearTile = BuildGlueNearTileGeometry(3, 5, 0);

	SECTION("far tile east of the near tile")
	{
		const GlueTerrainTileGeometry farTile = BuildGlueFarTileGeometry(4, 5, 0, [](const int32 blockX, int32) { return blockX < 0; });
		const auto nearEdge = GlueTerrainEdgeVertices(nearTile, 3, 5, GlueTerrainSide::East, PageX, PageZ, heights);
		const auto farEdge = GlueTerrainEdgeVertices(farTile, 4, 5, GlueTerrainSide::West, PageX, PageZ, heights);
		CHECK(nearEdge.size() == 9);
		CHECK(sameEdge(nearEdge, farEdge));
		CHECK_FALSE(hasTJunction(farTile));
		CHECK(allFaceUp(farTile));
	}

	SECTION("far tile north of the near tile")
	{
		const GlueTerrainTileGeometry farTile = BuildGlueFarTileGeometry(3, 4, 0, [](int32, const int32 blockZ) { return blockZ >= 4; });
		CHECK(sameEdge(
			GlueTerrainEdgeVertices(nearTile, 3, 5, GlueTerrainSide::North, PageX, PageZ, heights),
			GlueTerrainEdgeVertices(farTile, 3, 4, GlueTerrainSide::South, PageX, PageZ, heights)));
	}

	SECTION("far tile west of the near tile")
	{
		const GlueTerrainTileGeometry farTile = BuildGlueFarTileGeometry(2, 5, 0, [](const int32 blockX, int32) { return blockX >= 4; });
		CHECK(sameEdge(
			GlueTerrainEdgeVertices(nearTile, 3, 5, GlueTerrainSide::West, PageX, PageZ, heights),
			GlueTerrainEdgeVertices(farTile, 2, 5, GlueTerrainSide::East, PageX, PageZ, heights)));
	}

	SECTION("far tile south of the near tile")
	{
		const GlueTerrainTileGeometry farTile = BuildGlueFarTileGeometry(3, 6, 0, [](int32, const int32 blockZ) { return blockZ < 0; });
		CHECK(sameEdge(
			GlueTerrainEdgeVertices(nearTile, 3, 5, GlueTerrainSide::South, PageX, PageZ, heights),
			GlueTerrainEdgeVertices(farTile, 3, 6, GlueTerrainSide::North, PageX, PageZ, heights)));
	}

	SECTION("across a page border")
	{
		const GlueTerrainTileGeometry pageEdgeTile = BuildGlueNearTileGeometry(15, 7, 0);
		const GlueTerrainTileGeometry nextPageTile = BuildGlueFarTileGeometry(0, 7, 0, [](const int32 blockX, int32) { return blockX < 0; });
		const auto nearEdge = GlueTerrainEdgeVertices(pageEdgeTile, 15, 7, GlueTerrainSide::East, PageX, PageZ, heightsOfPage(PageX, PageZ));
		const auto farEdge = GlueTerrainEdgeVertices(nextPageTile, 0, 7, GlueTerrainSide::West, PageX + 1, PageZ, heightsOfPage(PageX + 1, PageZ));
		CHECK(nearEdge.size() == 9);
		CHECK(sameEdge(nearEdge, farEdge));
	}
}

TEST_CASE("Glue far tiles keep holes at full resolution without cracks", "[glue_terrain]")
{
	// A hole in cell (3, 3) puts block (1, 1) at full resolution.
	const GlueTerrainTileGeometry tile = BuildGlueFarTileGeometry(0, 0, holeBit(3, 3), [](int32, int32) { return false; });

	// 11 plain blocks of 4 triangles, 4 blocks next to the hole block with 5, and 3 full cells of 4.
	CHECK(tile.indices.size() == (11 * 4 + 4 * 5 + 3 * 4) * 3);
	CHECK_FALSE(hasTJunction(tile));
	CHECK(allFaceUp(tile));

	// The hole cell's inner vertex is never used.
	bool holeUsed = false;
	for (const uint32 index : tile.indices)
	{
		holeUsed = holeUsed || tile.vertices[index] == GlueTerrainGridVertex{ 3, 3, true };
	}
	CHECK_FALSE(holeUsed);

	// A hole block next to the tile border needs the full resolution edge on the neighbour's side too.
	const GlueTerrainTileGeometry borderHole = BuildGlueFarTileGeometry(1, 0, holeBit(0, 0), [](int32, int32) { return false; });
	const GlueTerrainTileGeometry westNeighbour = BuildGlueFarTileGeometry(0, 0, 0, [](const int32 blockX, const int32 blockZ) { return blockX == 4 && blockZ == 0; });
	const auto heights = heightsOfPage(PageX, PageZ);
	const auto holeSide = GlueTerrainEdgeVertices(borderHole, 1, 0, GlueTerrainSide::West, PageX, PageZ, heights);
	const auto neighbourSide = GlueTerrainEdgeVertices(westNeighbour, 0, 0, GlueTerrainSide::East, PageX, PageZ, heights);
	// The neighbour takes the middle vertex of the hole block's side. The hole tile's corner (8, 0) only
	// bounds the hole cell, so it is the one neighbour vertex the hole tile does not use.
	CHECK(neighbourSide.size() == 6);
	REQUIRE(holeSide.size() == 5);
	CHECK(sameEdge(holeSide, std::vector<Vector3>(neighbourSide.begin() + 1, neighbourSide.end())));
}
