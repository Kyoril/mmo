// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "assets/asset_registry.h"
#include "nav_mesh/map.h"
#include "nav_mesh/page_streamer.h"
#include "terrain/constants.h"

#include "DetourAlloc.h"
#include "DetourNavMeshBuilder.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>
#include <utility>
#include <vector>

using namespace mmo;

namespace
{
	// Mirror the constants of nav_mesh/map.cpp, which writes and checks the same file layout.
	constexpr uint32 FileSignature = 'NAVM';
	constexpr uint32 FileVersion = '0001';
	constexpr uint32 FilePage = 'PAGE';
	constexpr uint32 FileMap = 'MAP1';

	constexpr int32 TilesPerPage = static_cast<int32>(terrain::constants::TilesPerPage);
	constexpr float TileSize = static_cast<float>(terrain::constants::TileSize);
	constexpr float PageSize = static_cast<float>(terrain::constants::PageSize);
	constexpr float MapOrigin = -32.f * PageSize;

	/// Detour data for one tile: a single flat quad at y = 0 covering the whole tile, with all
	/// four edges marked as portals so neighbouring tiles link up.
	std::vector<uint8> BuildFlatTile(const int32 tileX, const int32 tileY)
	{
		constexpr unsigned short cells = 10;
		constexpr int nvp = 6;
		constexpr unsigned short nullIndex = 0xffff;

		const unsigned short verts[] = {
			0, 0, 0,
			0, 0, cells,
			cells, 0, cells,
			cells, 0, 0,
		};

		// Vertex indices, then per-edge neighbour info: 0x8000 | dir marks a portal edge
		// (0 = x-, 1 = z+, 2 = x+, 3 = z-), matching what Recast emits at tile borders.
		const unsigned short polys[nvp * 2] = {
			0, 1, 2, 3, nullIndex, nullIndex,
			0x8000 | 0, 0x8000 | 1, 0x8000 | 2, 0x8000 | 3, 0, 0,
		};
		const unsigned short polyFlags[] = { 1 };
		const unsigned char polyAreas[] = { 0 };

		dtNavMeshCreateParams params;
		std::memset(&params, 0, sizeof(params));
		params.verts = verts;
		params.vertCount = 4;
		params.polys = polys;
		params.polyFlags = polyFlags;
		params.polyAreas = polyAreas;
		params.polyCount = 1;
		params.nvp = nvp;
		params.tileX = tileX;
		params.tileY = tileY;
		params.bmin[0] = MapOrigin + static_cast<float>(tileX) * TileSize;
		params.bmin[1] = 0.f;
		params.bmin[2] = MapOrigin + static_cast<float>(tileY) * TileSize;
		params.bmax[0] = params.bmin[0] + TileSize;
		params.bmax[1] = 1.f;
		params.bmax[2] = params.bmin[2] + TileSize;
		params.cs = TileSize / cells;
		params.ch = 0.1f;
		params.walkableHeight = 2.f;
		params.walkableRadius = 0.5f;
		params.walkableClimb = 1.f;
		params.buildBvTree = true;

		unsigned char* data = nullptr;
		int dataSize = 0;
		REQUIRE(dtCreateNavMeshData(&params, &data, &dataSize));

		std::vector<uint8> result(data, data + dataSize);
		dtFree(data);
		return result;
	}

	template <typename T>
	void WritePod(std::ofstream& out, const T& value)
	{
		out.write(reinterpret_cast<const char*>(&value), sizeof(value));
	}

	/// A nav map on disk, registered as the only asset folder for the lifetime of the object.
	class SyntheticNavMap
	{
	public:
		/// @param pages Pages the map's bitmap lists.
		/// @param missingFiles Listed pages whose .nav file is not written.
		explicit SyntheticNavMap(const std::vector<std::pair<int32, int32>>& pages, const std::set<std::pair<int32, int32>>& missingFiles = {})
			: m_root(std::filesystem::temp_directory_path() / ("mmo_nav_streamer_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
		{
			std::filesystem::create_directories(m_root / Name);

			uint8 bitmap[terrain::constants::MaxPagesSquared / 8] = {};
			for (const auto& [x, y] : pages)
			{
				const uint32 offset = static_cast<uint32>(y) * terrain::constants::MaxPages + static_cast<uint32>(x);
				bitmap[offset / 8] |= static_cast<uint8>(1 << (offset % 8));

				if (missingFiles.count({ x, y }) == 0)
				{
					WritePage(x, y);
				}
			}

			std::ofstream mapFile(m_root / (std::string(Name) + ".map"), std::ios::binary);
			WritePod(mapFile, FileMap);
			WritePod(mapFile, static_cast<uint8>(1));
			mapFile.write(reinterpret_cast<const char*>(bitmap), sizeof(bitmap));
			mapFile.close();

			AssetRegistry::Initialize(m_root, {});
		}

		~SyntheticNavMap()
		{
			AssetRegistry::Destroy();

			std::error_code error;
			std::filesystem::remove_all(m_root, error);
		}

		static constexpr const char* Name = "StreamTest";

	private:
		void WritePage(const int32 pageX, const int32 pageY) const
		{
			char fileName[16];
			std::snprintf(fileName, sizeof(fileName), "%02d_%02d.nav", pageX, pageY);

			std::ofstream out(m_root / Name / fileName, std::ios::binary);

			nav::MapHeader header{};
			header.sig = FileSignature;
			header.ver = FileVersion;
			header.kind = FilePage;
			header.x = static_cast<uint32>(pageX);
			header.y = static_cast<uint32>(pageY);
			header.tileCount = TilesPerPage * TilesPerPage;
			WritePod(out, header);

			for (int32 y = pageY * TilesPerPage; y < (pageY + 1) * TilesPerPage; ++y)
			{
				for (int32 x = pageX * TilesPerPage; x < (pageX + 1) * TilesPerPage; ++x)
				{
					const std::vector<uint8> tile = BuildFlatTile(x, y);
					WritePod(out, x);
					WritePod(out, y);
					WritePod(out, static_cast<uint32>(tile.size()));
					out.write(reinterpret_cast<const char*>(tile.data()), static_cast<std::streamsize>(tile.size()));
				}
			}
		}

		std::filesystem::path m_root;
	};

	/// World position in the middle of a page, on the flat test mesh.
	Vector3 PageCenter(const int32 pageX, const int32 pageY)
	{
		return {
			MapOrigin + (static_cast<float>(pageX) + 0.5f) * PageSize,
			0.f,
			MapOrigin + (static_cast<float>(pageY) + 0.5f) * PageSize
		};
	}

	void StreamToCompletion(nav::PageStreamer& streamer)
	{
		const auto giveUp = std::chrono::steady_clock::now() + std::chrono::seconds(30);
		while (!streamer.InstallReadyPages(std::chrono::milliseconds(1)))
		{
			REQUIRE(std::chrono::steady_clock::now() < giveUp);
		}
	}

	const std::vector<std::pair<int32, int32>> RowOfPages = { { 30, 32 }, { 31, 32 }, { 32, 32 }, { 33, 32 } };
}

TEST_CASE("ReadPage and AddPage install a page that paths can be found on", "[nav_mesh]")
{
	SyntheticNavMap files(RowOfPages);
	nav::Map map(SyntheticNavMap::Name);

	nav::PageData page;
	REQUIRE(nav::Map::ReadPage(SyntheticNavMap::Name, 31, 32, page));
	CHECK(page.tiles.size() == static_cast<size_t>(TilesPerPage * TilesPerPage));
	CHECK_FALSE(map.IsPageLoaded(31, 32));

	REQUIRE(map.AddPage(std::move(page)));
	CHECK(map.IsPageLoaded(31, 32));

	const Vector3 start = PageCenter(31, 32) - Vector3(100.f, 0.f, 100.f);
	const Vector3 end = PageCenter(31, 32) + Vector3(100.f, 0.f, 100.f);
	std::vector<Vector3> path;
	REQUIRE(map.FindPath(start, end, path));
	CHECK(path.back().GetDistanceTo(end) < 1.f);
}

TEST_CASE("ReadPage rejects a page whose file is missing", "[nav_mesh]")
{
	SyntheticNavMap files(RowOfPages, { { 31, 32 } });

	nav::PageData page;
	CHECK_FALSE(nav::Map::ReadPage(SyntheticNavMap::Name, 31, 32, page));
}

TEST_CASE("Paths cross page borders once neighbouring pages are loaded", "[nav_mesh]")
{
	SyntheticNavMap files(RowOfPages);
	nav::Map map(SyntheticNavMap::Name);
	CHECK(map.LoadAllPages() == static_cast<int32>(RowOfPages.size()));

	const Vector3 start = PageCenter(30, 32);
	const Vector3 end = PageCenter(33, 32);
	std::vector<Vector3> path;
	REQUIRE(map.FindPath(start, end, path));
	CHECK(path.back().GetDistanceTo(end) < 1.f);
}

TEST_CASE("PageStreamer installs every page of the map", "[nav_mesh]")
{
	SyntheticNavMap files(RowOfPages);
	nav::Map map(SyntheticNavMap::Name);

	nav::PageStreamer streamer(map);
	CHECK(streamer.GetPageCount() == RowOfPages.size());

	StreamToCompletion(streamer);

	CHECK(streamer.IsComplete());
	CHECK(streamer.GetFailedPageCount() == 0);
	for (const auto& [x, y] : RowOfPages)
	{
		CHECK(map.IsPageLoaded(x, y));
	}

	std::vector<Vector3> path;
	CHECK(map.FindPath(PageCenter(30, 32), PageCenter(33, 32), path));
}

TEST_CASE("PageStreamer only installs pages on the owning thread", "[nav_mesh]")
{
	SyntheticNavMap files(RowOfPages);
	nav::Map map(SyntheticNavMap::Name);

	nav::PageStreamer streamer(map);

	// However far the background thread has got, nothing is in the nav mesh until the owner
	// installs it.
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	for (const auto& [x, y] : RowOfPages)
	{
		CHECK_FALSE(map.IsPageLoaded(x, y));
	}
}

TEST_CASE("PageStreamer::EnsureLoaded installs exactly the pages a query area overlaps", "[nav_mesh]")
{
	SyntheticNavMap files(RowOfPages);
	nav::Map map(SyntheticNavMap::Name);

	nav::PageStreamer streamer(map);

	// An area straddling the border between pages 31 and 32
	const Vector3 center = PageCenter(31, 32);
	streamer.EnsureLoaded(center.x, center.z - 10.f, center.x + PageSize, center.z + 10.f);

	CHECK(map.IsPageLoaded(31, 32));
	CHECK(map.IsPageLoaded(32, 32));
	CHECK_FALSE(map.IsPageLoaded(30, 32));
	CHECK_FALSE(map.IsPageLoaded(33, 32));
	CHECK_FALSE(streamer.IsComplete());

	std::vector<Vector3> path;
	CHECK(map.FindPath(PageCenter(31, 32), PageCenter(32, 32), path));

	// The hole EnsureLoaded exists to prevent: a destination on a page nobody installed yet
	CHECK_FALSE(map.FindPath(PageCenter(32, 32), PageCenter(33, 32), path));

	// Pages loaded on demand are not installed a second time by the stream
	StreamToCompletion(streamer);
	CHECK(streamer.GetFailedPageCount() == 0);
	for (const auto& [x, y] : RowOfPages)
	{
		CHECK(map.IsPageLoaded(x, y));
	}
}

TEST_CASE("PageStreamer::EnsureLoaded clamps areas outside the page grid", "[nav_mesh]")
{
	SyntheticNavMap files(RowOfPages);
	nav::Map map(SyntheticNavMap::Name);

	nav::PageStreamer streamer(map);
	streamer.EnsureLoaded(-1.e9f, -1.e9f, 1.e9f, 1.e9f);

	CHECK(streamer.IsComplete());
	for (const auto& [x, y] : RowOfPages)
	{
		CHECK(map.IsPageLoaded(x, y));
	}
}

TEST_CASE("PageStreamer completes when a listed page has no file", "[nav_mesh]")
{
	SyntheticNavMap files(RowOfPages, { { 31, 32 } });
	nav::Map map(SyntheticNavMap::Name);

	nav::PageStreamer streamer(map);
	StreamToCompletion(streamer);

	CHECK(streamer.GetFailedPageCount() == 1);
	CHECK_FALSE(map.IsPageLoaded(31, 32));
	CHECK(map.IsPageLoaded(30, 32));
	CHECK(map.IsPageLoaded(32, 32));

	// A failed page is given up on, not retried by every query that touches it
	const Vector3 center = PageCenter(31, 32);
	streamer.EnsureLoaded(center.x, center.z, center.x, center.z);
	CHECK(streamer.GetFailedPageCount() == 1);
}

TEST_CASE("PageStreamer skips pages that were loaded before it started", "[nav_mesh]")
{
	SyntheticNavMap files(RowOfPages);
	nav::Map map(SyntheticNavMap::Name);
	REQUIRE(map.LoadPage(31, 32));

	nav::PageStreamer streamer(map);
	CHECK(streamer.GetPageCount() == RowOfPages.size());

	StreamToCompletion(streamer);
	CHECK(streamer.GetFailedPageCount() == 0);
}

TEST_CASE("PageStreamer can be destroyed while it is still reading", "[nav_mesh]")
{
	SyntheticNavMap files(RowOfPages);
	nav::Map map(SyntheticNavMap::Name);

	{
		nav::PageStreamer streamer(map);
	}

	// The map stays usable; whatever was not installed is simply missing
	CHECK(map.LoadAllPages() == static_cast<int32>(RowOfPages.size()));
}
