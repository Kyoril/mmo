// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "memory_source.h"
#include "vector_sink.h"
#include "base/typedefs.h"
#include "math/math_utils.h"
#include "math/vector3.h"
#include "terrain_io/page_data.h"
#include "terrain_io/page_lod.h"
#include "writer.h"
#include "reader.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace mmo;

namespace
{
	constexpr uint32 outerSide = terrain::constants::OuterVerticesPerPageSide;

	/// A flat page at the given height whose normals all point straight up.
	terrain_io::PageData MakeFlatPage(const float height)
	{
		terrain_io::PageData data;
		data.Reset();

		for (size_t i = 0; i < data.heightmap.size(); ++i)
		{
			data.heightmap[i] = height;
			data.normals[i] = EncodeNormalSNorm8(0.0f, 1.0f, 0.0f);
		}

		return data;
	}

	Vector3 Decode(const EncodedNormal8& encoded)
	{
		Vector3 n;
		DecodeNormalSNorm8(encoded, n.x, n.y, n.z);
		return n;
	}
}

TEST_CASE("Page LOD grid is every fourth outer vertex", "[terrain_io][page_lod]")
{
	STATIC_REQUIRE(terrain_io::LodVertexStride == 4);
	STATIC_REQUIRE(terrain_io::LodVerticesPerPageSide == 33);
	STATIC_REQUIRE((terrain_io::LodVerticesPerPageSide - 1) * terrain_io::LodVertexStride == outerSide - 1);
}

TEST_CASE("Page LOD samples the outer heightmap at the LOD grid", "[terrain_io][page_lod]")
{
	terrain_io::PageData page = MakeFlatPage(0.0f);
	for (uint32 z = 0; z < outerSide; ++z)
	{
		for (uint32 x = 0; x < outerSide; ++x)
		{
			page.heightmap[x + z * outerSide] = static_cast<float>(x) * 10.0f + static_cast<float>(z);
		}
	}

	terrain_io::PageLodData lod;
	terrain_io::BuildPageLod(terrain_io::MakeLodSource(page), lod);

	REQUIRE(lod.heights.size() == terrain_io::LodVertexCount);
	for (uint32 j = 0; j < terrain_io::LodVerticesPerPageSide; ++j)
	{
		for (uint32 i = 0; i < terrain_io::LodVerticesPerPageSide; ++i)
		{
			const float expected = static_cast<float>(i * 4) * 10.0f + static_cast<float>(j * 4);
			CHECK(lod.heights[i + j * terrain_io::LodVerticesPerPageSide] == expected);
		}
	}
}

TEST_CASE("Page LOD lifts samples touching water to the water surface", "[terrain_io][page_lod]")
{
	// Sea floor at -20, surface at 0. Only the first sub-quad of the first tile carries water: it
	// spans outer vertices (0..1, 0..1), so only LOD vertex (0, 0) touches it.
	terrain_io::PageData page = MakeFlatPage(-20.0f);
	page.normals[0] = EncodeNormalSNorm8(1.0f, 0.0f, 0.0f);
	page.waterQuadMasks[0] = 1ull;
	page.waterTypes[0] = 2;
	std::fill(page.waterVertexHeights.begin(), page.waterVertexHeights.end(), 0.0f);

	terrain_io::PageLodData lod;
	terrain_io::BuildPageLod(terrain_io::MakeLodSource(page), lod);

	CHECK(lod.heights[0] == 0.0f);
	CHECK(lod.heights[1] == -20.0f);
	CHECK(lod.heights[terrain_io::LodVerticesPerPageSide] == -20.0f);

	// The surface is flat, whatever the sea floor's normals did.
	const Vector3 surfaceNormal = Decode(lod.normals[0]);
	CHECK(surfaceNormal.y == Approx(1.0f).margin(0.02f));
}

TEST_CASE("Page LOD keeps terrain that rises above the water surface", "[terrain_io][page_lod]")
{
	// A shore: water flagged under a vertex that already sits above the surface must not be pulled down.
	terrain_io::PageData page = MakeFlatPage(5.0f);
	page.waterQuadMasks[0] = 1ull;
	page.waterTypes[0] = 1;
	std::fill(page.waterVertexHeights.begin(), page.waterVertexHeights.end(), 0.0f);

	terrain_io::PageLodData lod;
	terrain_io::BuildPageLod(terrain_io::MakeLodSource(page), lod);

	CHECK(lod.heights[0] == 5.0f);
}

TEST_CASE("Page LOD water height is ignored without a quad mask", "[terrain_io][page_lod]")
{
	// Water heights are zero-initialised on every page; presence is the quad mask, never the height.
	terrain_io::PageData page = MakeFlatPage(-20.0f);
	std::fill(page.waterVertexHeights.begin(), page.waterVertexHeights.end(), 0.0f);

	terrain_io::PageLodData lod;
	terrain_io::BuildPageLod(terrain_io::MakeLodSource(page), lod);

	for (const float h : lod.heights)
	{
		CHECK(h == -20.0f);
	}
}

TEST_CASE("Page LOD normals average the full-resolution normals around a sample", "[terrain_io][page_lod]")
{
	// Alternate the full-resolution normals between two slopes. Point-sampling every fourth vertex
	// would alias to one of them; the LOD must see their average (straight up).
	terrain_io::PageData page = MakeFlatPage(0.0f);
	const float s = std::sqrt(0.5f);
	for (uint32 z = 0; z < outerSide; ++z)
	{
		for (uint32 x = 0; x < outerSide; ++x)
		{
			page.normals[x + z * outerSide] = (x % 2 == 0) ? EncodeNormalSNorm8(s, s, 0.0f) : EncodeNormalSNorm8(-s, s, 0.0f);
		}
	}

	terrain_io::PageLodData lod;
	terrain_io::BuildPageLod(terrain_io::MakeLodSource(page), lod);

	// An interior sample sees a symmetric 5x5 footprint (x-2..x+2): three of one slope and two of
	// the other per row, so it tilts a little, but far less than either input slope.
	const Vector3 n = Decode(lod.normals[8 + 8 * terrain_io::LodVerticesPerPageSide]);
	CHECK(n.y > 0.95f);
	CHECK(std::abs(n.x) < 0.3f);
}

TEST_CASE("Page LOD data round-trips through save and load", "[terrain_io][page_lod]")
{
	terrain_io::PageLodData original;
	original.Reset();
	for (size_t i = 0; i < original.heights.size(); ++i)
	{
		original.heights[i] = static_cast<float>(i) * 0.75f - 300.0f;
		original.normals[i] = EncodedNormal8{ static_cast<int8_t>(i % 100), 90, static_cast<int8_t>(-(static_cast<int>(i) % 77)) };
	}

	std::vector<char> buffer;
	{
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		REQUIRE(terrain_io::SavePageLod(writer, original));
	}

	io::MemorySource source{ buffer };
	io::Reader reader{ source };
	terrain_io::PageLodData loaded;
	REQUIRE(terrain_io::LoadPageLod(reader, loaded));

	CHECK(original.heights == loaded.heights);
	CHECK(std::memcmp(original.normals.data(), loaded.normals.data(), original.normals.size() * sizeof(EncodedNormal8)) == 0);
}

TEST_CASE("Page LOD load rejects data without heights", "[terrain_io][page_lod]")
{
	// A terrain page file is not a LOD file: its chunks are unknown here and the required ones missing.
	const terrain_io::PageData page = MakeFlatPage(1.0f);
	terrain_io::PageLodData lod;
	terrain_io::BuildPageLod(terrain_io::MakeLodSource(page), lod);

	std::vector<char> buffer;
	{
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		REQUIRE(terrain_io::SavePageLod(writer, lod));
	}

	// Truncate after the version chunk (8 byte header + 4 byte version).
	buffer.resize(12);

	io::MemorySource source{ buffer };
	io::Reader reader{ source };
	terrain_io::PageLodData loaded;
	CHECK_FALSE(terrain_io::LoadPageLod(reader, loaded));
}
