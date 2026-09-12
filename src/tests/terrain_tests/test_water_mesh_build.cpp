// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "terrain/water_mesh_build.h"

#include <vector>

using namespace mmo;
using namespace mmo::terrain;
using namespace mmo::terrain::water_mesh;

namespace
{
	struct PageFixture
	{
		std::vector<uint64> masks;
		std::vector<uint8> types;
		std::vector<float> heights;

		PageFixture()
			: masks(constants::TilesPerPage * constants::TilesPerPage, 0ULL)
			, types(constants::TilesPerPage * constants::TilesPerPage, static_cast<uint8>(WaterType::None))
			, heights(constants::OuterVerticesPerPageSide * constants::OuterVerticesPerPageSide, 0.0f)
		{
		}

		[[nodiscard]] water_lookup::PageWaterView View() const
		{
			return water_lookup::PageWaterView{ masks.data(), types.data(), heights.data() };
		}

		void SetTileFull(const uint32 tileX, const uint32 tileZ, const WaterType type)
		{
			const size_t index = tileX + tileZ * constants::TilesPerPage;
			masks[index] = ~0ULL;
			types[index] = static_cast<uint8>(type);
		}

		void SetQuad(const uint32 tileX, const uint32 tileZ, const uint32 qx, const uint32 qz, const WaterType type)
		{
			const size_t index = tileX + tileZ * constants::TilesPerPage;
			masks[index] |= (1ULL << (qx + qz * water_lookup::QuadsPerTileSide));
			types[index] = static_cast<uint8>(type);
		}
	};
}

TEST_CASE("WaterMeshBuild_Empty_Page_Produces_No_Batches", "[water_mesh]")
{
	const PageFixture page;
	CHECK(BucketQuadsByType(page.View()).empty());
}

TEST_CASE("WaterMeshBuild_Null_View_Produces_No_Batches", "[water_mesh]")
{
	CHECK(BucketQuadsByType(water_lookup::PageWaterView{ nullptr, nullptr, nullptr }).empty());
}

TEST_CASE("WaterMeshBuild_Single_Type_Is_One_Batch", "[water_mesh]")
{
	PageFixture page;
	page.SetTileFull(0, 0, WaterType::Ocean);

	const std::vector<TypeBatch> batches = BucketQuadsByType(page.View());

	REQUIRE(batches.size() == 1);
	CHECK(batches[0].type == WaterType::Ocean);
	// A fully flooded tile is 8x8 sub-quads.
	CHECK(batches[0].quads.size() == 64);
}

TEST_CASE("WaterMeshBuild_Two_Types_Are_Two_Batches_In_Type_Order", "[water_mesh]")
{
	PageFixture page;
	// Deliberately author Ocean first so a stable result proves the sort rather than the
	// traversal order.
	page.SetTileFull(0, 0, WaterType::Ocean);
	page.SetTileFull(1, 0, WaterType::Water);

	const std::vector<TypeBatch> batches = BucketQuadsByType(page.View());

	REQUIRE(batches.size() == 2);
	CHECK(batches[0].type == WaterType::Water);
	CHECK(batches[1].type == WaterType::Ocean);
	CHECK(batches[0].quads.size() == 64);
	CHECK(batches[1].quads.size() == 64);
}

TEST_CASE("WaterMeshBuild_Same_Type_Across_Tiles_Is_One_Batch", "[water_mesh]")
{
	PageFixture page;
	page.SetTileFull(0, 0, WaterType::Ocean);
	page.SetTileFull(5, 9, WaterType::Ocean);

	const std::vector<TypeBatch> batches = BucketQuadsByType(page.View());

	REQUIRE(batches.size() == 1);
	CHECK(batches[0].type == WaterType::Ocean);
	CHECK(batches[0].quads.size() == 128);
}

TEST_CASE("WaterMeshBuild_Skips_Unset_Quads", "[water_mesh]")
{
	PageFixture page;
	page.SetQuad(2, 3, 1, 1, WaterType::Ocean);
	page.SetQuad(2, 3, 4, 5, WaterType::Ocean);

	const std::vector<TypeBatch> batches = BucketQuadsByType(page.View());

	REQUIRE(batches.size() == 1);
	REQUIRE(batches[0].quads.size() == 2);

	CHECK(batches[0].quads[0].tileX == 2);
	CHECK(batches[0].quads[0].tileZ == 3);
	CHECK(batches[0].quads[0].qx == 1);
	CHECK(batches[0].quads[0].qz == 1);
	CHECK(batches[0].quads[1].qx == 4);
	CHECK(batches[0].quads[1].qz == 5);
}

TEST_CASE("WaterMeshBuild_Tile_With_Type_But_No_Quads_Is_Omitted", "[water_mesh]")
{
	// Erasing every quad of a tile without clearing its type byte must not produce an empty
	// batch: RebuildWaterMesh would create a render operation with no triangles, and
	// ManualTriangleListOperation::Finish asserts on exactly that.
	PageFixture page;
	const size_t index = 5 + 5 * constants::TilesPerPage;
	page.types[index] = static_cast<uint8>(WaterType::Ocean);
	page.masks[index] = 0ULL;

	CHECK(BucketQuadsByType(page.View()).empty());
}

TEST_CASE("WaterMeshBuild_Is_Deterministic_Across_Calls", "[water_mesh]")
{
	// Pages are rebuilt on every stream-in, so two runs over identical data must agree
	// exactly or translucent draw order flickers as the player walks away and back.
	PageFixture page;
	page.SetTileFull(4, 4, WaterType::Ocean);
	page.SetTileFull(4, 5, WaterType::Water);
	page.SetQuad(9, 2, 3, 3, WaterType::Slime);

	const std::vector<TypeBatch> first = BucketQuadsByType(page.View());
	const std::vector<TypeBatch> second = BucketQuadsByType(page.View());

	REQUIRE(first.size() == second.size());
	for (size_t b = 0; b < first.size(); ++b)
	{
		CHECK(first[b].type == second[b].type);
		REQUIRE(first[b].quads.size() == second[b].quads.size());
		for (size_t q = 0; q < first[b].quads.size(); ++q)
		{
			CHECK(first[b].quads[q].tileX == second[b].quads[q].tileX);
			CHECK(first[b].quads[q].tileZ == second[b].quads[q].tileZ);
			CHECK(first[b].quads[q].qx == second[b].quads[q].qx);
			CHECK(first[b].quads[q].qz == second[b].quads[q].qz);
		}
	}
}

TEST_CASE("WaterMeshBuild_Face_Alpha_Tags_Differ", "[water_mesh]")
{
	// The material graph keys the underside look off vertex colour alpha, so the two tags
	// must not collide, and must sit either side of the midpoint the graph compares against.
	CHECK(TopFaceVertexAlpha != BottomFaceVertexAlpha);
	CHECK(TopFaceVertexAlpha > 0x80u);
	CHECK(BottomFaceVertexAlpha < 0x80u);
}
