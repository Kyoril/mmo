// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "terrain/terrain_region_math.h"

using namespace mmo;
using namespace mmo::terrain::region_math;

TEST_CASE("ClampToBounds_Clamps_Negative_Origin", "[terrain_region]")
{
	// 1x1 page terrain = 128x128 cells
	const VertexRect r = ClampToBounds(VertexRect{ -10, -5, 50, 50 }, 1, 1);
	CHECK(r.minX == 0);
	CHECK(r.minZ == 0);
	CHECK(r.sizeX == 40);
	CHECK(r.sizeZ == 45);
}

TEST_CASE("ClampToBounds_Clamps_Overshoot", "[terrain_region]")
{
	const VertexRect r = ClampToBounds(VertexRect{ 100, 120, 50, 50 }, 1, 1);
	CHECK(r.minX == 100);
	CHECK(r.sizeX == 28);
	CHECK(r.minZ == 120);
	CHECK(r.sizeZ == 8);
}

TEST_CASE("ClampToBounds_Empty_When_Fully_Outside", "[terrain_region]")
{
	const VertexRect r = ClampToBounds(VertexRect{ 200, 0, 50, 50 }, 1, 1);
	CHECK(r.IsEmpty());
}

TEST_CASE("Vertex_World_Roundtrip", "[terrain_region]")
{
	// 2x2 pages: vertex 0 is at -PageSize, vertex 256 at +PageSize.
	const float w0 = VertexToWorld(0, 2);
	const float wMid = VertexToWorld(128, 2);
	CHECK(w0 == Approx(-terrain::constants::PageSize));
	CHECK(wMid == Approx(0.0));
	CHECK(RoundWorldToVertex(w0, 2) == 0);
	CHECK(RoundWorldToVertex(wMid + 0.1f, 2) == 128);
	// Clamped at bounds
	CHECK(RoundWorldToVertex(-1e6f, 2) == 0);
	CHECK(RoundWorldToVertex(1e6f, 2) == 256);
}

TEST_CASE("PixelRangeInside_Full_Page_Is_Full_Pixel_Grid", "[terrain_region]")
{
	int32 minPX, minPZ, maxPX, maxPZ;
	PixelRangeInside(VertexRect{ 0, 0, 128, 128 }, minPX, minPZ, maxPX, maxPZ);
	CHECK(minPX == 0);
	CHECK(minPZ == 0);
	CHECK(maxPX == 1008);
	CHECK(maxPZ == 1008);
}

TEST_CASE("PixelRangeInside_Uses_Exact_63_Over_8_Ratio", "[terrain_region]")
{
	// 8 vertex cells = exactly 63 pixel cells
	int32 minPX, minPZ, maxPX, maxPZ;
	PixelRangeInside(VertexRect{ 8, 16, 8, 8 }, minPX, minPZ, maxPX, maxPZ);
	CHECK(minPX == 63);
	CHECK(maxPX == 126);
	CHECK(minPZ == 126);
	CHECK(maxPZ == 189);
	// Non-aligned rect: pixels strictly inside the world span
	PixelRangeInside(VertexRect{ 1, 0, 1, 1 }, minPX, minPZ, maxPX, maxPZ);
	// 1 cell spans pixels [7.875, 15.75] -> inside range [8, 15]
	CHECK(minPX == 8);
	CHECK(maxPX == 15);
}

TEST_CASE("PixelRangePadded_Covers_World_Span", "[terrain_region]")
{
	int32 minPX, minPZ, maxPX, maxPZ;
	PixelRangePadded(VertexRect{ 1, 1, 1, 1 }, minPX, minPZ, maxPX, maxPZ);
	// world span [7.875, 15.75] -> padded [7, 16]
	CHECK(minPX == 7);
	CHECK(maxPX == 16);
	CHECK(minPZ == 7);
	CHECK(maxPZ == 16);
}

TEST_CASE("MapDestPixelToSourcePixel_Identity_And_Shift", "[terrain_region]")
{
	CHECK(MapDestPixelToSourcePixel(500, 32, 32) == 500);
	// 8-cell shift = exactly 63 pixels
	CHECK(MapDestPixelToSourcePixel(500, 40, 32) == 563);
	// 1-cell shift = 7.875 pixels, rounded
	CHECK(MapDestPixelToSourcePixel(500, 33, 32) == 508);
}

TEST_CASE("TileRangeFullyCovered_Aligned_And_Partial", "[terrain_region]")
{
	int32 minTX, minTZ, maxTX, maxTZ;
	// Tile-aligned rect (tiles are 8 cells): cells [8,24) = tiles [1,2]
	CHECK(TileRangeFullyCovered(VertexRect{ 8, 8, 16, 16 }, minTX, minTZ, maxTX, maxTZ));
	CHECK(minTX == 1);
	CHECK(maxTX == 2);
	// Off-by-one shrinks to fully covered tiles only
	CHECK(TileRangeFullyCovered(VertexRect{ 9, 8, 16, 16 }, minTX, minTZ, maxTX, maxTZ));
	CHECK(minTX == 2);
	CHECK(maxTX == 2);
	// Too small to cover any tile
	CHECK(!TileRangeFullyCovered(VertexRect{ 9, 9, 6, 6 }, minTX, minTZ, maxTX, maxTZ));
}

TEST_CASE("MapDestTileToSourceTile_Identity_And_Shift", "[terrain_region]")
{
	CHECK(MapDestTileToSourceTile(5, 16, 16) == 5);
	// 8-cell (one tile) shift
	CHECK(MapDestTileToSourceTile(5, 24, 16) == 6);
	// 4-cell (half-tile) shift: sampled center cell 5*8+4+4 = 48 lands in tile 6
	CHECK(MapDestTileToSourceTile(5, 20, 16) == 6);
}

TEST_CASE("CoonsHeight_Reproduces_Borders", "[terrain_region]")
{
	// Flat field: everything 3
	CHECK(CoonsHeight(0.3f, 0.7f, 3, 3, 3, 3, 3, 3, 3, 3) == Approx(3.0f));
	// u=0 must return the left border value regardless of right
	// (top/bottom evaluated at u=0 must equal corners h00/h01 for consistency)
	const float left = 5.0f;
	CHECK(CoonsHeight(0.0f, 0.5f, left, 99.0f, 5.0f, 5.0f, 5.0f, 7.0f, 5.0f, 7.0f) == Approx(left));
	// v=0 must return the top border value
	const float top = 4.0f;
	CHECK(CoonsHeight(0.5f, 0.0f, 4.0f, 4.0f, top, 99.0f, 4.0f, 4.0f, 8.0f, 8.0f) == Approx(top));
}

TEST_CASE("VertexRectForBrush_Covers_Footprint", "[terrain_region]")
{
	// 1x1 page terrain, brush at world center with radius = 1 cell size
	const float cellSize = static_cast<float>(terrain::constants::PageSize) / 128.0f;
	const VertexRect r = VertexRectForBrush(0.0f, 0.0f, cellSize, 1, 1);
	// Center is vertex 64; radius 1 cell -> at least cells [63,65)
	CHECK(r.minX <= 63);
	CHECK(r.minX + r.sizeX >= 65);
	CHECK(!r.IsEmpty());
}

namespace
{
	float FanSum(const CellFanWeights& w)
	{
		return w.w00 + w.w10 + w.w01 + w.w11 + w.centre;
	}

	// Reconstructs the point from its weights, using the cell's vertex positions.
	void FanPoint(const CellFanWeights& w, float& u, float& v)
	{
		u = w.w10 + w.w11 + 0.5f * w.centre;
		v = w.w01 + w.w11 + 0.5f * w.centre;
	}
}

TEST_CASE("CellFanWeights_Reproduce_Every_Vertex", "[terrain_region]")
{
	CHECK(ComputeCellFanWeights(0.5f, 0.5f).centre == Approx(1.0f));
	CHECK(ComputeCellFanWeights(0.0f, 0.0f).w00 == Approx(1.0f));
	CHECK(ComputeCellFanWeights(1.0f, 0.0f).w10 == Approx(1.0f));
	CHECK(ComputeCellFanWeights(0.0f, 1.0f).w01 == Approx(1.0f));
	CHECK(ComputeCellFanWeights(1.0f, 1.0f).w11 == Approx(1.0f));
}

TEST_CASE("CellFanWeights_Edge_Midpoint_Ignores_Centre", "[terrain_region]")
{
	// On a cell edge only that edge's two corners contribute, never the centre: the edge is
	// shared with the neighbouring cell, which has a different centre.
	const CellFanWeights w = ComputeCellFanWeights(0.5f, 0.0f);
	CHECK(w.centre == Approx(0.0f).margin(1e-6));
	CHECK(w.w00 == Approx(0.5f));
	CHECK(w.w10 == Approx(0.5f));
}

TEST_CASE("CellFanWeights_Are_Barycentric", "[terrain_region]")
{
	for (float u = 0.0f; u <= 1.0f; u += 0.0625f)
	{
		for (float v = 0.0f; v <= 1.0f; v += 0.0625f)
		{
			const CellFanWeights w = ComputeCellFanWeights(u, v);
			CHECK(FanSum(w) == Approx(1.0f));
			CHECK(w.w00 >= 0.0f);
			CHECK(w.w10 >= 0.0f);
			CHECK(w.w01 >= 0.0f);
			CHECK(w.w11 >= 0.0f);
			CHECK(w.centre >= 0.0f);

			float pu, pv;
			FanPoint(w, pu, pv);
			CHECK(pu == Approx(u).margin(1e-5));
			CHECK(pv == Approx(v).margin(1e-5));
		}
	}
}
