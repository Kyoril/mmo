// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/world/solid_visibility_grid.h"
#include "game_server/world/tiled_unit_finder.h"
#include "game_server/world/tiled_unit_finder_tile.h"
#include "game_server/world/visibility_tile.h"
#include "test_unit_factory.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

#include <limits>

using namespace mmo;

// A movement packet carries a client-chosen position. Anything the server cannot map onto its
// grids used to index them out of range: GetTilePosition's failure was ignored, the resulting
// null tile was guarded by ASSERT only, and the unit finder never bounds-checked at all. One
// packet with x = 1e7 took a release world node down. These tests pin the grids to failing
// safely, so a bad position that slips past packet validation cannot corrupt memory.

namespace
{
	// Matches WorldInstanceManager::CreateInstance.
	constexpr int32 MaxWorldSize = 64;
	constexpr float FinderTileWidth = 33.3333f;
}

TEST_CASE("VisibilityGrid rejects positions outside the map but still yields an in-range tile", "[world_grid]")
{
	SolidVisibilityGrid grid(makeVector(MaxWorldSize, MaxWorldSize));

	TileIndex2D index;
	REQUIRE(grid.GetTilePosition(Vector3(0.0f, 0.0f, 0.0f), index[0], index[1]));
	CHECK(grid.GetTile(index) != nullptr);

	const float bad[] = { 1.0e7f, -1.0e7f, std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
		std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() };

	for (const float value : bad)
	{
		INFO("value = " << value);

		CHECK_FALSE(grid.GetTilePosition(Vector3(value, 0.0f, 0.0f), index[0], index[1]));
		CHECK(grid.GetTile(index) != nullptr);
		CHECK(&grid.RequireTile(index) == grid.GetTile(index));

		CHECK_FALSE(grid.GetTilePosition(Vector3(0.0f, 0.0f, value), index[0], index[1]));
		CHECK(grid.GetTile(index) != nullptr);
	}
}

TEST_CASE("VisibilityGrid RequireTile survives an out-of-range index", "[world_grid]")
{
	SolidVisibilityGrid grid(makeVector(MaxWorldSize, MaxWorldSize));

	// Must not write outside the tile storage; it lands on the nearest edge tile instead.
	VisibilityTile& tile = grid.RequireTile(TileIndex2D(1 << 20, -(1 << 20)));
	CHECK(&tile == grid.GetTile(tile.GetPosition()));
}

TEST_CASE("TiledUnitFinder bounds check agrees with the visibility grid", "[world_grid]")
{
	TiledUnitFinder finder(FinderTileWidth);

	CHECK(finder.IsInBounds(Vector3(0.0f, 0.0f, 0.0f)));
	CHECK(finder.IsInBounds(Vector3(-15000.0f, 100.0f, 15000.0f)));

	CHECK_FALSE(finder.IsInBounds(Vector3(1.0e7f, 0.0f, 0.0f)));
	CHECK_FALSE(finder.IsInBounds(Vector3(0.0f, 0.0f, -1.0e7f)));
	CHECK_FALSE(finder.IsInBounds(Vector3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f)));
	CHECK_FALSE(finder.IsInBounds(Vector3(std::numeric_limits<float>::infinity(), 0.0f, 0.0f)));
}

TEST_CASE("TiledUnitFinder keeps a unit moved far outside the map without corrupting memory", "[world_grid]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	TiledUnitFinder finder(FinderTileWidth);

	const auto outsider = test::MakeUnit(project, timers);
	outsider->Relocate(Vector3(1.0e7f, 0.0f, -1.0e7f), Radian(0.0f));
	finder.AddUnit(*outsider);

	const auto insider = test::MakeUnit(project, timers);
	insider->Relocate(Vector3(0.0f, 0.0f, 0.0f), Radian(0.0f));
	finder.AddUnit(*insider);

	// Move the outsider back in and out again: OnUnitMoved re-tiles it each time.
	const Vector3 outsiderPos = outsider->GetPosition();
	outsider->Relocate(Vector3(10.0f, 0.0f, 10.0f), Radian(0.0f));
	finder.UpdatePosition(*outsider, outsiderPos);
	outsider->Relocate(Vector3(-1.0e7f, 0.0f, 1.0e7f), Radian(0.0f));
	finder.UpdatePosition(*outsider, Vector3(10.0f, 0.0f, 10.0f));

	size_t found = 0;
	finder.FindUnits(Circle(0.0f, 0.0f, 50.0f), [&found](GameUnitS&) { ++found; return true; });
	CHECK(found == 1);

	finder.RemoveUnit(*outsider);
	finder.RemoveUnit(*insider);
}
