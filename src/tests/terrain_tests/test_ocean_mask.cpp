// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "terrain/ocean_mask.h"

#include <cmath>
#include <vector>

using namespace mmo;
using namespace mmo::terrain;

namespace
{
	constexpr float texel = static_cast<float>(ocean_mask::TexelSize);
	constexpr uint32 outerSide = constants::OuterVerticesPerPageSide;

	/// One prepared page's arrays, owned so the view stays valid.
	struct PreparedPage
	{
		std::vector<uint64> masks;
		std::vector<uint8> types;
		std::vector<float> waterHeights;
		std::vector<float> heights;

		explicit PreparedPage(const float groundHeight)
			: masks(constants::TilesPerPage * constants::TilesPerPage, 0ULL)
			, types(constants::TilesPerPage * constants::TilesPerPage, static_cast<uint8>(WaterType::None))
			, waterHeights(outerSide * outerSide, 0.0f)
			, heights(outerSide * outerSide, groundHeight)
		{
		}

		/// Flags page-local quad (x, z) as water of the given type (the type applies to its whole tile).
		void SetQuad(const uint32 quadX, const uint32 quadZ, const WaterType type)
		{
			const uint32 tile = quadX / water_lookup::QuadsPerTileSide + (quadZ / water_lookup::QuadsPerTileSide) * constants::TilesPerPage;
			masks[tile] |= 1ULL << (quadX % water_lookup::QuadsPerTileSide + (quadZ % water_lookup::QuadsPerTileSide) * water_lookup::QuadsPerTileSide);
			types[tile] = static_cast<uint8>(type);
		}

		/// Sets the four outer vertices around page-local quad (x, z) to a height.
		void SetQuadGround(const uint32 quadX, const uint32 quadZ, const float height)
		{
			heights[quadX + quadZ * outerSide] = height;
			heights[quadX + 1 + quadZ * outerSide] = height;
			heights[quadX + (quadZ + 1) * outerSide] = height;
			heights[quadX + 1 + (quadZ + 1) * outerSide] = height;
		}

		[[nodiscard]] ocean_mask::PageSource Source() const
		{
			ocean_mask::PageSource source;
			source.state = ocean_mask::PageState::Prepared;
			source.water = water_lookup::PageWaterView{ masks.data(), types.data(), waterHeights.data() };
			source.heights = heights.data();
			return source;
		}
	};

	struct Mask
	{
		std::vector<uint8> rg;
		float originX = 0.0f;
		float originZ = 0.0f;
		uint32 side = 0;

		[[nodiscard]] uint8 R(const uint32 x, const uint32 z) const { return rg[(z * side + x) * ocean_mask::ChannelsPerTexel]; }
		[[nodiscard]] uint8 G(const uint32 x, const uint32 z) const { return rg[(z * side + x) * ocean_mask::ChannelsPerTexel + 1]; }
	};

	/// World coordinate of the centre of global quad index q on a terrain pages wide.
	float QuadCenter(const int32 q, const uint32 pages)
	{
		const int32 halfQuads = static_cast<int32>(pages / 2 * ocean_mask::QuadsPerPageSide);
		return static_cast<float>((static_cast<double>(q - halfQuads) + 0.5) * ocean_mask::TexelSize);
	}
}

TEST_CASE("Ocean mask depth encoding", "[terrain][ocean_mask]")
{
	CHECK(ocean_mask::EncodeDepth(0.0f, 3.0f) == 0);
	CHECK(ocean_mask::EncodeDepth(0.0f, 0.0f) == 0);
	CHECK(ocean_mask::EncodeDepth(0.0f, -16.0f) == 128);
	CHECK(ocean_mask::EncodeDepth(0.0f, -ocean_mask::DepthEncodeRange) == 255);
	CHECK(ocean_mask::EncodeDepth(0.0f, -500.0f) == 255);
	CHECK(ocean_mask::EncodeDepth(10.0f, 2.0f) == 64);
}

TEST_CASE("Ocean mask texel is one water quad", "[terrain][ocean_mask]")
{
	STATIC_REQUIRE(ocean_mask::QuadsPerPageSide == 128);
	CHECK(texel == Approx(water_lookup::QuadSize));
}

TEST_CASE("Ocean mask window snaps to the quad grid", "[terrain][ocean_mask]")
{
	const auto unavailable = [](uint32, uint32) { return ocean_mask::PageSource{}; };

	Mask mask;
	mask.side = 8;
	ocean_mask::Build(10.3f, -7.9f, mask.side, 64, 64, 0.0f, unavailable, mask.rg, mask.originX, mask.originZ);

	REQUIRE(mask.rg.size() == 8u * 8u * 2u);

	// floor(10.3 / 4.1667) = 2, minus half the window: quad -2. floor(-7.9 / 4.1667) = -2: quad -6.
	CHECK(mask.originX == Approx(-2.0f * texel));
	CHECK(mask.originZ == Approx(-6.0f * texel));

	// The centre lies inside the window, half a window from its corner.
	CHECK(mask.originX <= 10.3f);
	CHECK(mask.originX + mask.side * texel > 10.3f);
}

TEST_CASE("Ocean mask treats off-map and unavailable pages as open ocean", "[terrain][ocean_mask]")
{
	uint32 calls = 0;
	const auto unavailable = [&calls](uint32, uint32)
	{
		++calls;
		return ocean_mask::PageSource{};
	};

	Mask mask;
	mask.side = 16;

	// Straddles the western map edge of a 2x2 page terrain: half off the map, half unavailable.
	ocean_mask::Build(QuadCenter(0, 2), QuadCenter(5, 2), mask.side, 2, 2, 0.0f, unavailable, mask.rg, mask.originX, mask.originZ);

	for (uint32 z = 0; z < mask.side; ++z)
	{
		for (uint32 x = 0; x < mask.side; ++x)
		{
			CHECK(mask.R(x, z) == 255);
			CHECK(mask.G(x, z) == 255);
		}
	}

	// The on-map half lies in one page, which is resolved once per run of texels, not per texel.
	CHECK(calls > 0);
	CHECK(calls <= mask.side);
}

TEST_CASE("Ocean mask draws placeholder pages as deep ocean", "[terrain][ocean_mask]")
{
	const auto placeholder = [](uint32, uint32)
	{
		ocean_mask::PageSource source;
		source.state = ocean_mask::PageState::OceanPlaceholder;
		return source;
	};

	Mask mask;
	mask.side = 4;
	ocean_mask::Build(0.0f, 0.0f, mask.side, 64, 64, 0.0f, placeholder, mask.rg, mask.originX, mask.originZ);

	for (uint32 z = 0; z < mask.side; ++z)
	{
		for (uint32 x = 0; x < mask.side; ++x)
		{
			CHECK(mask.R(x, z) == 255);
			CHECK(mask.G(x, z) == 255);
		}
	}
}

TEST_CASE("Ocean mask draws only ocean quads of prepared pages", "[terrain][ocean_mask]")
{
	// Ground 16 below the sea level everywhere.
	PreparedPage page(-16.0f);

	// Tile 0 is ocean: quads 0, 4 and 6 carry water, quad 2 does not. They are spaced apart because
	// neighbouring quads share their corner vertices.
	page.SetQuad(0, 0, WaterType::Ocean);
	page.SetQuad(4, 0, WaterType::Ocean);
	page.SetQuad(6, 0, WaterType::Ocean);

	// Quad 4 sits over ground deeper than the encode range, quad 6 over ground above the sea level.
	page.SetQuadGround(4, 0, -100.0f);
	page.SetQuadGround(6, 0, 5.0f);

	// Tile 1 (quads 8..15) is a lake: water, but not ocean.
	page.SetQuad(8, 0, WaterType::Water);
	page.SetQuad(9, 0, WaterType::Water);

	uint32 resolvedX = 99;
	uint32 resolvedZ = 99;
	const auto resolve = [&](const uint32 pageX, const uint32 pageZ)
	{
		resolvedX = pageX;
		resolvedZ = pageZ;
		return page.Source();
	};

	// A 2x2 page terrain: page (0, 0) starts at global quad 0. Window of 16 starting there.
	Mask mask;
	mask.side = 16;
	ocean_mask::Build(QuadCenter(8, 2), QuadCenter(8, 2), mask.side, 2, 2, 0.0f, resolve, mask.rg, mask.originX, mask.originZ);

	CHECK(resolvedX == 0);
	CHECK(resolvedZ == 0);
	CHECK(mask.originX == Approx(-static_cast<float>(constants::PageSize)));
	CHECK(mask.originZ == Approx(-static_cast<float>(constants::PageSize)));

	// Ocean over ground 16 deep: half the encode range.
	CHECK(mask.R(0, 0) == 255);
	CHECK(mask.G(0, 0) == 128);

	// Ocean deeper than the range saturates.
	CHECK(mask.R(4, 0) == 255);
	CHECK(mask.G(4, 0) == 255);

	// Ocean flagged over dry ground: drawn, zero depth (the depth test hides it).
	CHECK(mask.R(6, 0) == 255);
	CHECK(mask.G(6, 0) == 0);

	// An ocean tile's quad without its water bit is not ocean.
	CHECK(mask.R(2, 0) == 0);
	CHECK(mask.G(2, 0) == 0);

	// Lake quads are meshed by the page, not drawn by the ocean renderer.
	CHECK(mask.R(8, 0) == 0);
	CHECK(mask.R(9, 0) == 0);

	// Dry quads.
	CHECK(mask.R(5, 5) == 0);
	CHECK(mask.G(5, 5) == 0);
}

TEST_CASE("Ocean mask rows run along Z", "[terrain][ocean_mask]")
{
	PreparedPage page(-32.0f);
	page.SetQuad(0, 1, WaterType::Ocean);

	const auto resolve = [&page](uint32, uint32) { return page.Source(); };

	Mask mask;
	mask.side = 4;
	ocean_mask::Build(QuadCenter(2, 2), QuadCenter(2, 2), mask.side, 2, 2, 0.0f, resolve, mask.rg, mask.originX, mask.originZ);

	// Global quad (0, 1) is texel x = 0, z = 1: byte (1 * 4 + 0) * 2.
	CHECK(mask.rg[(1 * 4 + 0) * 2] == 255);
	CHECK(mask.rg[(1 * 4 + 0) * 2 + 1] == 255);
	CHECK(mask.R(1, 0) == 0);
	CHECK(mask.R(0, 0) == 0);
}
