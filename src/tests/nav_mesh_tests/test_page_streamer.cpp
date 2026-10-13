// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "synthetic_nav_map.h"

#include "nav_mesh/page_streamer.h"

#include <limits>
#include <thread>

using namespace mmo;
using namespace test_nav;

namespace
{
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

TEST_CASE("PageStreamer::EnsureLoaded ignores non-finite areas", "[nav_mesh]")
{
	SyntheticNavMap files(RowOfPages);
	nav::Map map(SyntheticNavMap::Name);

	nav::PageStreamer streamer(map);
	const float nan = std::numeric_limits<float>::quiet_NaN();
	streamer.EnsureLoaded(nan, 0.f, nan, 0.f);
	streamer.EnsureLoaded(-std::numeric_limits<float>::infinity(), 0.f, 0.f, 0.f);

	for (const auto& [x, y] : RowOfPages)
	{
		CHECK_FALSE(map.IsPageLoaded(x, y));
	}
}

TEST_CASE("AddPage keeps the installed tile when a page repeats a tile", "[nav_mesh]")
{
	SyntheticNavMap files(RowOfPages);
	nav::Map map(SyntheticNavMap::Name);

	nav::PageData page;
	REQUIRE(nav::Map::ReadPage(SyntheticNavMap::Name, 31, 32, page));
	const nav::PageData::TileData first = page.tiles.front();
	page.tiles.push_back(first);
	REQUIRE(map.AddPage(std::move(page)));

	// Both ends inside the repeated tile: it must still be in the nav mesh
	const Vector3 tileMin(MapOrigin + static_cast<float>(first.x) * TileSize, 0.f, MapOrigin + static_cast<float>(first.y) * TileSize);
	std::vector<Vector3> path;
	CHECK(map.FindPath(tileMin + Vector3(5.f, 0.f, 5.f), tileMin + Vector3(25.f, 0.f, 25.f), path));
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
