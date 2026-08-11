// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "memory_source.h"
#include "vector_sink.h"
#include "base/typedefs.h"
#include "terrain_io/page_data.h"
#include "terrain_io/page_io.h"
#include "writer.h"
#include "reader.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace mmo;

namespace
{
	constexpr size_t outerVertexCount = terrain::constants::OuterVerticesPerPageSide * terrain::constants::OuterVerticesPerPageSide;
	constexpr size_t innerVertexCount = terrain::constants::InnerVerticesPerPageSide * terrain::constants::InnerVerticesPerPageSide;
	constexpr size_t tileCount = terrain::constants::TilesPerPage * terrain::constants::TilesPerPage;

	terrain_io::PageData MakePatternedPage()
	{
		terrain_io::PageData data;
		data.Reset();

		for (size_t i = 0; i < outerVertexCount; ++i)
		{
			data.heightmap[i] = static_cast<float>(i) * 0.25f - 100.0f;
			data.normals[i] = EncodedNormal8{ static_cast<int8_t>(i % 127), 100, static_cast<int8_t>(-(static_cast<int>(i) % 100)) };
			data.colors[i] = static_cast<uint32>(i * 2654435761u);
			data.waterVertexHeights[i] = static_cast<float>(i % 32) * 0.5f;
		}

		for (size_t i = 0; i < innerVertexCount; ++i)
		{
			data.innerHeightmap[i] = static_cast<float>(i) * -0.125f + 42.0f;
			data.innerNormals[i] = EncodedNormal8{ static_cast<int8_t>(-(static_cast<int>(i) % 90)), 90, static_cast<int8_t>(i % 90) };
			data.innerColors[i] = static_cast<uint32>(i * 2246822519u);
		}

		for (size_t i = 0; i < data.layers.size(); ++i)
		{
			data.layers[i] = static_cast<uint32>(i * 3266489917u);
		}

		for (size_t i = 0; i < tileCount; ++i)
		{
			data.zones[i] = static_cast<uint32>(i * 7 + 1);
			data.materialNames[i] = (i % 5 == 0) ? "Models/TestMaterial_" + std::to_string(i) + ".hmat" : String();
		}

		// Sparse holes and water
		data.holes[3] = 0x00000000000000F0ull;
		data.holes[200] = 0xFFFFFFFFFFFFFFFFull;
		data.waterQuadMasks[17] = 0x0F0F0F0F0F0F0F0Full;
		data.waterTypes[17] = 2;
		data.waterMaterialName = "Worlds/Water_Base.hmat";

		return data;
	}
}

TEST_CASE("Terrain page data round-trips through save and load", "[terrain_io]")
{
	const terrain_io::PageData original = MakePatternedPage();

	std::vector<char> buffer;
	io::VectorSink sink{ buffer };
	io::Writer writer{ sink };
	REQUIRE(terrain_io::SavePage(writer, terrain_io::MakeView(original)));

	io::MemorySource source{ buffer };
	io::Reader reader{ source };
	terrain_io::PageData loaded;
	REQUIRE(terrain_io::LoadPage(reader, loaded));

	CHECK(original.heightmap == loaded.heightmap);
	CHECK(original.innerHeightmap == loaded.innerHeightmap);
	CHECK(std::memcmp(original.normals.data(), loaded.normals.data(), original.normals.size() * sizeof(EncodedNormal8)) == 0);
	CHECK(std::memcmp(original.innerNormals.data(), loaded.innerNormals.data(), original.innerNormals.size() * sizeof(EncodedNormal8)) == 0);
	CHECK(original.materialNames == loaded.materialNames);
	CHECK(original.layers == loaded.layers);
	CHECK(original.colors == loaded.colors);
	CHECK(original.innerColors == loaded.innerColors);
	CHECK(original.holes == loaded.holes);
	CHECK(original.zones == loaded.zones);
	CHECK(original.waterQuadMasks == loaded.waterQuadMasks);
	CHECK(original.waterTypes == loaded.waterTypes);
	CHECK(original.waterVertexHeights == loaded.waterVertexHeights);
	CHECK(original.waterMaterialName == loaded.waterMaterialName);
}

TEST_CASE("Saving a saved page twice produces identical bytes", "[terrain_io]")
{
	const terrain_io::PageData original = MakePatternedPage();

	std::vector<char> firstBuffer;
	{
		io::VectorSink sink{ firstBuffer };
		io::Writer writer{ sink };
		REQUIRE(terrain_io::SavePage(writer, terrain_io::MakeView(original)));
	}

	io::MemorySource source{ firstBuffer };
	io::Reader reader{ source };
	terrain_io::PageData loaded;
	REQUIRE(terrain_io::LoadPage(reader, loaded));

	std::vector<char> secondBuffer;
	{
		io::VectorSink sink{ secondBuffer };
		io::Writer writer{ sink };
		REQUIRE(terrain_io::SavePage(writer, terrain_io::MakeView(loaded)));
	}

	REQUIRE(firstBuffer.size() == secondBuffer.size());
	CHECK(std::memcmp(firstBuffer.data(), secondBuffer.data(), firstBuffer.size()) == 0);
}

TEST_CASE("Editor-saved golden page survives load and save byte-identically", "[terrain_io]")
{
	// Locate an editor-saved page in the project data. This proves the standalone
	// serialization stays byte-compatible with what the editor produces. The test is
	// skipped gracefully when the game data is not present (e.g. CI server builds).
	const char *candidates[] = {
		"data/client/Worlds/Collision/Collision/Terrain/30_30.tile",
		"../data/client/Worlds/Collision/Collision/Terrain/30_30.tile",
		"../../data/client/Worlds/Collision/Collision/Terrain/30_30.tile"
	};

	std::filesystem::path goldenPath;
	for (const char *candidate : candidates)
	{
		if (std::filesystem::exists(candidate))
		{
			goldenPath = candidate;
			break;
		}
	}

	if (goldenPath.empty())
	{
		WARN("Golden .tile file not found, skipping byte-compatibility test");
		return;
	}

	std::ifstream file{ goldenPath, std::ios::binary };
	REQUIRE(file.is_open());
	std::vector<char> goldenBytes{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
	REQUIRE(!goldenBytes.empty());

	io::MemorySource source{ goldenBytes };
	io::Reader reader{ source };
	terrain_io::PageData loaded;
	REQUIRE(terrain_io::LoadPage(reader, loaded));

	std::vector<char> savedBytes;
	{
		io::VectorSink sink{ savedBytes };
		io::Writer writer{ sink };
		REQUIRE(terrain_io::SavePage(writer, terrain_io::MakeView(loaded)));
	}

	REQUIRE(savedBytes.size() == goldenBytes.size());
	CHECK(std::memcmp(savedBytes.data(), goldenBytes.data(), savedBytes.size()) == 0);
}
