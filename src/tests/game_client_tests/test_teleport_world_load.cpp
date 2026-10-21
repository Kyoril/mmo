// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_client/world_streaming_utils.h"
#include "terrain/constants.h"

using namespace mmo;

namespace
{
	/// Center of the terrain page with the given indices, at an arbitrary height.
	Vector3 PageCenter(const int32 pageX, const int32 pageZ, const float height = 5.0f)
	{
		const float pageSize = static_cast<float>(terrain::constants::PageSize);
		return Vector3(
			(static_cast<float>(pageX) - 32.0f) * pageSize + pageSize * 0.5f,
			height,
			(static_cast<float>(pageZ) - 32.0f) * pageSize + pageSize * 0.5f);
	}
}

TEST_CASE("Terrain page index matches the page streaming grid", "[teleport_load]")
{
	// The page grid is centered on page 32, so the world origin sits in page 32/32 and the
	// page index decreases with every page size step towards negative coordinates.
	REQUIRE(GetTerrainPageIndex(0.0f) == 32);
	REQUIRE(GetTerrainPageIndex(static_cast<float>(terrain::constants::PageSize) - 1.0f) == 32);
	REQUIRE(GetTerrainPageIndex(static_cast<float>(terrain::constants::PageSize) + 1.0f) == 33);
	REQUIRE(GetTerrainPageIndex(-1.0f) == 31);
}

TEST_CASE("A short teleport does not wait for the world", "[teleport_load]")
{
	// A blink or a short dev port stays well inside the pages which are already streamed in,
	// so there is ground to land on and no loading screen is warranted.
	const Vector3 from(233.86f, 1.57f, 551.41f);
	const Vector3 to(288.25f, 5.33f, 552.57f);

	REQUIRE_FALSE(TeleportNeedsWorldLoad(from, to));
}

TEST_CASE("A teleport into the neighboring page does not wait for the world", "[teleport_load]")
{
	// The page the player stands on plus its direct neighbors are kept loaded, so a teleport
	// which only crosses a page border still lands on ground that is already there.
	REQUIRE_FALSE(TeleportNeedsWorldLoad(PageCenter(32, 32), PageCenter(33, 32)));
	REQUIRE_FALSE(TeleportNeedsWorldLoad(PageCenter(32, 32), PageCenter(31, 33)));
}

TEST_CASE("A teleport across the map waits for the world", "[teleport_load]")
{
	// Nothing outside the streamed square exists on the client yet: the character is flagged
	// as falling by the teleport and would drop through the missing terrain while the pages
	// stream in. Such a teleport has to load the world around its destination first.
	REQUIRE(TeleportNeedsWorldLoad(PageCenter(32, 32), PageCenter(34, 32)));
	REQUIRE(TeleportNeedsWorldLoad(PageCenter(32, 32), PageCenter(32, 36)));
	REQUIRE(TeleportNeedsWorldLoad(PageCenter(40, 40), PageCenter(20, 20)));

	// Direction does not matter.
	REQUIRE(TeleportNeedsWorldLoad(PageCenter(34, 32), PageCenter(32, 32)));
}
