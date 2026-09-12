// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "terrain/water_lookup.h"

#include <vector>

using namespace mmo;
using namespace mmo::terrain;
using namespace mmo::terrain::water_lookup;

namespace
{
	/// A page's water arrays, owned by the fixture so the view stays valid.
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

		[[nodiscard]] PageWaterView View() const
		{
			return PageWaterView{ masks.data(), types.data(), heights.data() };
		}

		void SetQuad(const uint32 tileX, const uint32 tileZ, const uint32 qx, const uint32 qz, const WaterType type)
		{
			const size_t index = tileX + tileZ * constants::TilesPerPage;
			masks[index] |= (1ULL << (qx + qz * QuadsPerTileSide));
			types[index] = static_cast<uint8>(type);
		}

		void SetTileFull(const uint32 tileX, const uint32 tileZ, const WaterType type)
		{
			const size_t index = tileX + tileZ * constants::TilesPerPage;
			masks[index] = ~0ULL;
			types[index] = static_cast<uint8>(type);
		}
	};
}

TEST_CASE("WaterLookup_Empty_Page_Has_No_Water", "[water_lookup]")
{
	const PageFixture page;
	const PageWaterView view = page.View();

	CHECK_FALSE(HasWaterAtQuad(view, 0, 0, 0, 0));
	CHECK_FALSE(HasWaterAtQuad(view, 15, 15, 7, 7));
	CHECK(TypeAtTile(view, 0, 0) == WaterType::None);
}

TEST_CASE("WaterLookup_Null_View_Is_Water_Free", "[water_lookup]")
{
	// A page that never carried a water chunk, or one currently unloaded, presents null
	// pointers. This must not crash and must not report water.
	const PageWaterView view{ nullptr, nullptr, nullptr };

	CHECK_FALSE(HasWaterAtQuad(view, 0, 0, 0, 0));
	CHECK(TypeAtTile(view, 0, 0) == WaterType::None);
}

TEST_CASE("WaterLookup_Finds_Single_Quad", "[water_lookup]")
{
	PageFixture page;
	page.SetQuad(3, 4, 5, 6, WaterType::Ocean);
	const PageWaterView view = page.View();

	CHECK(HasWaterAtQuad(view, 3, 4, 5, 6));
	CHECK(TypeAtTile(view, 3, 4) == WaterType::Ocean);

	// Neighbouring quads inside the same tile stay dry.
	CHECK_FALSE(HasWaterAtQuad(view, 3, 4, 4, 6));
	CHECK_FALSE(HasWaterAtQuad(view, 3, 4, 5, 5));

	// A different tile is dry and untyped.
	CHECK_FALSE(HasWaterAtQuad(view, 3, 5, 5, 6));
	CHECK(TypeAtTile(view, 3, 5) == WaterType::None);
}

TEST_CASE("WaterLookup_Quad_Bits_Do_Not_Alias_Across_Tiles", "[water_lookup]")
{
	// Guards the qx + qz * 8 bit packing: setting one quad must not appear to set the
	// mirrored coordinate.
	PageFixture page;
	page.SetQuad(0, 0, 1, 0, WaterType::Ocean);
	const PageWaterView view = page.View();

	CHECK(HasWaterAtQuad(view, 0, 0, 1, 0));
	CHECK_FALSE(HasWaterAtQuad(view, 0, 0, 0, 1));
}

TEST_CASE("WaterLookup_Out_Of_Range_Is_Water_Free", "[water_lookup]")
{
	PageFixture page;
	page.SetTileFull(0, 0, WaterType::Ocean);
	const PageWaterView view = page.View();

	CHECK_FALSE(HasWaterAtQuad(view, constants::TilesPerPage, 0, 0, 0));
	CHECK_FALSE(HasWaterAtQuad(view, 0, constants::TilesPerPage, 0, 0));
	CHECK_FALSE(HasWaterAtQuad(view, 0, 0, QuadsPerTileSide, 0));
	CHECK_FALSE(HasWaterAtQuad(view, 0, 0, 0, QuadsPerTileSide));
	CHECK(TypeAtTile(view, constants::TilesPerPage, 0) == WaterType::None);
}

TEST_CASE("WaterLookup_Quad_From_Page_Local_Origin", "[water_lookup]")
{
	const QuadCoord quad = QuadFromPageLocal(0.0f, 0.0f);

	CHECK(quad.valid);
	CHECK(quad.tileX == 0);
	CHECK(quad.tileZ == 0);
	CHECK(quad.qx == 0);
	CHECK(quad.qz == 0);
}

TEST_CASE("WaterLookup_Quad_From_Page_Local_Second_Quad", "[water_lookup]")
{
	const QuadCoord quad = QuadFromPageLocal(QuadSize * 1.5f, QuadSize * 0.5f);

	CHECK(quad.valid);
	CHECK(quad.tileX == 0);
	CHECK(quad.tileZ == 0);
	CHECK(quad.qx == 1);
	CHECK(quad.qz == 0);
}

TEST_CASE("WaterLookup_Quad_From_Page_Local_Crosses_Tile", "[water_lookup]")
{
	// Eight quads per tile side, so the ninth quad along X is tile 1 quad 0.
	const QuadCoord quad = QuadFromPageLocal(QuadSize * 8.5f, QuadSize * 16.5f);

	CHECK(quad.valid);
	CHECK(quad.tileX == 1);
	CHECK(quad.qx == 0);
	CHECK(quad.tileZ == 2);
	CHECK(quad.qz == 0);
}

TEST_CASE("WaterLookup_Quad_From_Page_Local_Reaches_Last_Quad", "[water_lookup]")
{
	// Just inside the far corner of the page must still resolve, and must land on the last
	// quad rather than falling off the end.
	const float almostPageSize = static_cast<float>(constants::PageSize) - (QuadSize * 0.5f);
	const QuadCoord quad = QuadFromPageLocal(almostPageSize, almostPageSize);

	CHECK(quad.valid);
	CHECK(quad.tileX == constants::TilesPerPage - 1);
	CHECK(quad.tileZ == constants::TilesPerPage - 1);
	CHECK(quad.qx == QuadsPerTileSide - 1);
	CHECK(quad.qz == QuadsPerTileSide - 1);
}

TEST_CASE("WaterLookup_Quad_From_Page_Local_Rejects_Outside", "[water_lookup]")
{
	CHECK_FALSE(QuadFromPageLocal(-1.0f, 0.0f).valid);
	CHECK_FALSE(QuadFromPageLocal(0.0f, -1.0f).valid);
	CHECK_FALSE(QuadFromPageLocal(static_cast<float>(constants::PageSize) + 1.0f, 0.0f).valid);
	CHECK_FALSE(QuadFromPageLocal(0.0f, static_cast<float>(constants::PageSize) + 1.0f).valid);
}

TEST_CASE("WaterLookup_Type_Byte_Survives_Erased_Quads", "[water_lookup]")
{
	// A tile can keep its type byte after every quad under it has been erased. TypeAtTile
	// reports the stale type, which is why callers deciding "am I in water?" must gate on
	// HasWaterAtQuad first rather than treating a non-None type as presence.
	PageFixture page;
	const size_t index = 7 + 7 * constants::TilesPerPage;
	page.types[index] = static_cast<uint8>(WaterType::Ocean);
	page.masks[index] = 0ULL;
	const PageWaterView view = page.View();

	CHECK(TypeAtTile(view, 7, 7) == WaterType::Ocean);
	CHECK_FALSE(HasWaterAtQuad(view, 7, 7, 0, 0));
}
