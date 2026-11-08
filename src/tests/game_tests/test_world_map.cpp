// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game/world_map.h"

#include "binary_io/reader.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"
#include "binary_io/container_source.h"

using namespace mmo;
using namespace mmo::world_map;

namespace
{
	WorldMapInfo MakeTwoZoneMap()
	{
		WorldMapInfo info;
		info.minPageX = 30;
		info.minPageZ = 31;
		info.maxPageX = 31;
		info.maxPageZ = 31;
		info.pages[31 * MaxPages + 30] = true;
		info.pages[31 * MaxPages + 31] = true;
		info.ResetCells();

		// Left page zone 7, right page zone 9, with one cell of zone 0
		for (uint32 z = 31 * CellsPerPage; z < 32 * CellsPerPage; ++z)
		{
			for (uint32 x = 30 * CellsPerPage; x < 32 * CellsPerPage; ++x)
			{
				info.SetCell(x, z, x < 31 * CellsPerPage ? 7 : 9);
			}
		}
		info.SetCell(31 * CellsPerPage + 3, 31 * CellsPerPage + 3, 0);
		info.RebuildZones();
		return info;
	}
}

TEST_CASE("World map cells map world positions to zones", "[world_map]")
{
	const WorldMapInfo info = MakeTwoZoneMap();

	// Page 30 spans world x [-2 * PageSize, -PageSize), page 31 [-PageSize, 0)
	CHECK(info.GetZoneAt(-1.5 * PageSize, -0.5 * PageSize) == 7);
	CHECK(info.GetZoneAt(-0.5 * PageSize, -0.5 * PageSize) == 9);
	CHECK(info.GetZoneAt(10.0, 10.0) == 0);
	CHECK(info.GetZoneAt(-PageSize + 3.5 * CellSize, -PageSize + 3.5 * CellSize) == 0);

	REQUIRE(info.zones.size() == 2);
	const ZoneInfo* left = info.FindZone(7);
	REQUIRE(left != nullptr);
	CHECK(left->cellCount == CellsPerPage * CellsPerPage);
	CHECK(left->bounds.minX == Approx(-2.0 * PageSize));
	CHECK(left->bounds.maxX == Approx(-PageSize));
	CHECK(left->bounds.Contains(left->labelX, left->labelZ));
	CHECK(info.GetZoneAt(left->labelX, left->labelZ) == 7);

	// Largest zone first
	CHECK(info.zones.front().zoneId == 7);
}

TEST_CASE("World map info survives serialization", "[world_map]")
{
	const WorldMapInfo info = MakeTwoZoneMap();

	std::vector<char> buffer;
	io::VectorSink<char> sink(buffer);
	io::Writer writer(sink);
	writer << info;

	io::ContainerSource<std::vector<char>> source(buffer);
	io::Reader reader(source);
	WorldMapInfo loaded;
	REQUIRE(reader >> loaded);

	CHECK(loaded.minPageX == 30);
	CHECK(loaded.maxPageZ == 31);
	CHECK(loaded.HasPage(30, 31));
	CHECK_FALSE(loaded.HasPage(29, 31));
	CHECK(loaded.GetCells() == info.GetCells());
	REQUIRE(loaded.zones.size() == info.zones.size());
	CHECK(loaded.zones[0].labelX == Approx(info.zones[0].labelX));
}

TEST_CASE("World map rejects truncated or foreign data", "[world_map]")
{
	const WorldMapInfo info = MakeTwoZoneMap();
	std::vector<char> buffer;
	io::VectorSink<char> sink(buffer);
	io::Writer writer(sink);
	writer << info;

	buffer.resize(buffer.size() / 2);
	io::ContainerSource<std::vector<char>> source(buffer);
	io::Reader reader(source);
	WorldMapInfo loaded;
	CHECK_FALSE(reader >> loaded);

	std::vector<char> garbage(64, 'x');
	io::ContainerSource<std::vector<char>> garbageSource(garbage);
	io::Reader garbageReader(garbageSource);
	CHECK_FALSE(garbageReader >> loaded);
}

TEST_CASE("World map art tiles exist where any covered page has terrain", "[world_map]")
{
	const WorldMapInfo info = MakeTwoZoneMap();
	CHECK(info.HasArtTile(1, 15, 15));
	CHECK_FALSE(info.HasArtTile(1, 14, 15));
	CHECK(info.HasArtTile(6, 0, 0));
}

TEST_CASE("World map picks sharper art levels when zooming in", "[world_map]")
{
	CHECK(SelectArtLevel(0.5, 6) == MinArtLevel);
	CHECK(SelectArtLevel(ArtUnitsPerPixel(3), 6) == 3);
	CHECK(SelectArtLevel(1000.0, 6) == 6);
	CHECK(SelectArtLevel(1000.0, 4) == 4);

	// Minimap tiles only show when zoomed in
	CHECK(MinimapBlend(10.0) == 0.0f);
	CHECK(MinimapBlend(1.0) == 1.0f);
	const float mid = MinimapBlend(2.4);
	CHECK(mid > 0.0f);
	CHECK(mid < 1.0f);
}

TEST_CASE("World map zoom keeps the point under the cursor", "[world_map]")
{
	View view;
	view.width = 800;
	view.height = 600;
	view.centerX = 100.0;
	view.centerZ = -50.0;
	view.unitsPerPixel = 10.0;

	const double worldX = view.ScreenToWorldX(200.0);
	const double worldZ = view.ScreenToWorldZ(450.0);
	view.ZoomAt(200.0, 450.0, 2.5);
	CHECK(view.ScreenToWorldX(200.0) == Approx(worldX));
	CHECK(view.ScreenToWorldZ(450.0) == Approx(worldZ));
	CHECK(view.WorldToScreenX(worldX) == Approx(200.0));
}

TEST_CASE("World map view stays on the map", "[world_map]")
{
	const WorldRect bounds{ 0.0, 0.0, 8000.0, 4000.0 };
	View view;
	view.width = 1000;
	view.height = 500;

	// Zooming out further than the whole map stops at the map
	view.unitsPerPixel = 1000.0;
	ClampView(view, bounds, MinUnitsPerPixel);
	CHECK(view.unitsPerPixel == Approx(8.8));
	CHECK(view.centerX == Approx(4000.0));
	CHECK(view.centerZ == Approx(2000.0));

	// Zooming in has a floor, and the view cannot wander off the map
	view.unitsPerPixel = 0.001;
	view.centerX = -100000.0;
	ClampView(view, bounds, MinUnitsPerPixel);
	CHECK(view.unitsPerPixel == Approx(MinUnitsPerPixel));
	CHECK(view.GetVisibleRect().maxX > bounds.minX);
}

TEST_CASE("Minimap markers outside the circle are clamped to its edge", "[world_map]")
{
	float dx = 30.0f;
	float dz = 40.0f;
	CHECK(ClampToCircle(dx, dz, 10.0f));
	CHECK(dx == Approx(6.0f));
	CHECK(dz == Approx(8.0f));

	dx = 3.0f;
	dz = 4.0f;
	CHECK_FALSE(ClampToCircle(dx, dz, 10.0f));
	CHECK(dx == 3.0f);
}
