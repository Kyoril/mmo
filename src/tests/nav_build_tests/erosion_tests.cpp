// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "nav_build/common.h"
#include "nav_build/erosion.h"

#include "Recast.h"

#include <vector>

using namespace mmo;

namespace
{
	constexpr float CellSize = 0.25f;
	constexpr int Radius = 2;
	constexpr int FloorRadius = 1;

	/// Two rooms of 3 by 4 units joined by a corridor 0.75 units (three voxels) wide, the floor
	/// rasterized with the given area. The corridor is narrower than twice the full radius.
	struct Corridor
	{
		rcContext context { false };
		rcHeightfield* field { rcAllocHeightfield() };
		rcCompactHeightfield* compact { rcAllocCompactHeightfield() };

		explicit Corridor(const uint8 area)
		{
			const float bmin[3] = { 0.0f, -1.0f, 0.0f };
			const float bmax[3] = { 8.0f, 3.0f, 4.0f };
			REQUIRE(rcCreateHeightfield(&context, *field, 32, 16, bmin, bmax, CellSize, CellSize));

			const float vertices[] = {
				0.0f, 0.0f, 0.0f,   3.0f, 0.0f, 0.0f,   3.0f, 0.0f, 4.0f,   0.0f, 0.0f, 4.0f,
				5.0f, 0.0f, 0.0f,   8.0f, 0.0f, 0.0f,   8.0f, 0.0f, 4.0f,   5.0f, 0.0f, 4.0f,
				2.9f, 0.0f, 1.76f,  5.1f, 0.0f, 1.76f,  5.1f, 0.0f, 2.49f,  2.9f, 0.0f, 2.49f,
			};
			const int triangles[] = { 0, 2, 1, 0, 3, 2, 4, 6, 5, 4, 7, 6, 8, 10, 9, 8, 11, 10 };
			const unsigned char areas[] = { area, area, area, area, area, area };
			REQUIRE(rcRasterizeTriangles(&context, vertices, 12, triangles, areas, 6, *field, 2));
			REQUIRE(rcBuildCompactHeightfield(&context, 7, 2, *field, *compact));
		}

		~Corridor()
		{
			rcFreeCompactHeightfield(compact);
			rcFreeHeightField(field);
		}

		/// Area of the span in the column containing (x, z) after erosion.
		uint8 AreaAt(const float x, const float z) const
		{
			const rcCompactCell& cell = compact->cells[static_cast<int>(x / CellSize) + static_cast<int>(z / CellSize) * compact->width];
			REQUIRE(cell.count == 1);
			return compact->areas[cell.index];
		}
	};
}

TEST_CASE("A building floor keeps a corridor narrower than the agent open", "[nav_build][erosion]")
{
	Corridor corridor(poly_flags::WorldModel);
	REQUIRE(ErodeWalkableArea(corridor.context, *corridor.compact, Radius, FloorRadius, poly_flags::WorldModel));

	CHECK(corridor.AreaAt(4.0f, 2.125f) == poly_flags::WorldModel);
	CHECK(corridor.AreaAt(0.375f, 1.0f) == poly_flags::WorldModel);
	CHECK(corridor.AreaAt(0.125f, 1.0f) == RC_NULL_AREA);
}

TEST_CASE("Terrain keeps the full clearance and closes the same corridor", "[nav_build][erosion]")
{
	Corridor corridor(poly_flags::Ground);
	REQUIRE(ErodeWalkableArea(corridor.context, *corridor.compact, Radius, FloorRadius, poly_flags::WorldModel));

	CHECK(corridor.AreaAt(4.0f, 2.125f) == RC_NULL_AREA);
	CHECK(corridor.AreaAt(0.375f, 1.0f) == RC_NULL_AREA);
	CHECK(corridor.AreaAt(1.5f, 1.0f) == poly_flags::Ground);
}

TEST_CASE("Without narrow areas the erosion is Recast's own", "[nav_build][erosion]")
{
	Corridor corridor(poly_flags::WorldModel);
	REQUIRE(ErodeWalkableArea(corridor.context, *corridor.compact, Radius, FloorRadius, 0));

	CHECK(corridor.AreaAt(4.0f, 2.125f) == RC_NULL_AREA);
	CHECK(corridor.AreaAt(1.5f, 1.0f) == poly_flags::WorldModel);
}
