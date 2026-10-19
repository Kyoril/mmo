// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "nav_build/common.h"
#include "nav_build/door_marking.h"
#include "nav_mesh/door_volume.h"
#include "nav_mesh/instance_query_filter.h"

#include "Recast.h"

#include <memory>

using namespace mmo;

namespace
{
	/// Runs Recast on a flat 30 x 30 m floor at y = 0, with the given doors marked after erosion,
	/// using the nav builder's cell size and region settings. Returns the poly mesh.
	std::unique_ptr<rcPolyMesh, decltype(&rcFreePolyMesh)> BuildFloor(const std::vector<nav::DoorVolume>& doors)
	{
		rcContext ctx(false);

		rcConfig config {};
		config.cs = 33.3333f / 128.f;
		config.ch = 0.25f;
		config.walkableSlopeAngle = 75.f;
		config.walkableHeight = 7;
		config.walkableClimb = 2;
		config.walkableRadius = 2;
		config.maxEdgeLen = 46;
		config.maxSimplificationError = 0.8f;
		config.minRegionArea = 400;
		config.mergeRegionArea = 200;
		config.maxVertsPerPoly = 6;
		config.bmin[0] = 0.f; config.bmin[1] = -1.f; config.bmin[2] = 0.f;
		config.bmax[0] = 30.f; config.bmax[1] = 5.f; config.bmax[2] = 30.f;
		rcCalcGridSize(config.bmin, config.bmax, config.cs, &config.width, &config.height);

		std::unique_ptr<rcHeightfield, decltype(&rcFreeHeightField)> solid(rcAllocHeightfield(), rcFreeHeightField);
		REQUIRE(rcCreateHeightfield(&ctx, *solid, config.width, config.height, config.bmin, config.bmax, config.cs, config.ch));

		const float verts[] = { 0.f, 0.f, 0.f,  0.f, 0.f, 30.f,  30.f, 0.f, 30.f,  30.f, 0.f, 0.f };
		const int tris[] = { 0, 1, 2,  0, 2, 3 };
		const unsigned char areas[] = { poly_flags::Ground, poly_flags::Ground };
		REQUIRE(rcRasterizeTriangles(&ctx, verts, 4, tris, areas, 2, *solid, config.walkableClimb));

		std::unique_ptr<rcCompactHeightfield, decltype(&rcFreeCompactHeightfield)> chf(rcAllocCompactHeightfield(), rcFreeCompactHeightfield);
		REQUIRE(rcBuildCompactHeightfield(&ctx, config.walkableHeight, config.walkableClimb, *solid, *chf));
		REQUIRE(rcErodeWalkableArea(&ctx, config.walkableRadius, *chf));

		MarkDoorAreas(ctx, *chf, doors);

		REQUIRE(rcBuildDistanceField(&ctx, *chf));
		REQUIRE(rcBuildRegions(&ctx, *chf, 0, config.minRegionArea, config.mergeRegionArea));

		std::unique_ptr<rcContourSet, decltype(&rcFreeContourSet)> contours(rcAllocContourSet(), rcFreeContourSet);
		REQUIRE(rcBuildContours(&ctx, *chf, config.maxSimplificationError, config.maxEdgeLen, *contours, 0));

		std::unique_ptr<rcPolyMesh, decltype(&rcFreePolyMesh)> mesh(rcAllocPolyMesh(), rcFreePolyMesh);
		REQUIRE(rcBuildPolyMesh(&ctx, *contours, config.maxVertsPerPoly, *mesh));
		return mesh;
	}

	/// A 6 m wide barrier across the floor at z = 15, as ComputeDoorVolume inflates it.
	nav::DoorVolume Barrier()
	{
		nav::DoorVolume volume;
		volume.center = Vector3(15.f, 0.f, 15.f);
		volume.axisX = Vector3::UnitX;
		volume.axisZ = Vector3::UnitZ;
		volume.halfX = 3.f + nav::DoorVolumeInflate;
		volume.halfZ = nav::DoorVolumeInflate;
		volume.minY = -nav::DoorVolumeHeightPadding;
		volume.maxY = 7.f + nav::DoorVolumeHeightPadding;
		return volume;
	}

	/// World XZ center of a poly of a Recast poly mesh.
	Vector3 PolyCenter(const rcPolyMesh& mesh, const int poly)
	{
		Vector3 sum = Vector3::Zero;
		int count = 0;
		for (int v = 0; v < mesh.nvp; ++v)
		{
			const unsigned short index = mesh.polys[poly * mesh.nvp * 2 + v];
			if (index == RC_MESH_NULL_IDX)
			{
				break;
			}

			const unsigned short* vert = &mesh.verts[index * 3];
			sum += Vector3(mesh.bmin[0] + vert[0] * mesh.cs, 0.f, mesh.bmin[2] + vert[2] * mesh.cs);
			++count;
		}
		return sum / static_cast<float>(count);
	}
}

TEST_CASE("Door poly flag matches the nav mesh's door flag", "[nav_build][door]")
{
	STATIC_REQUIRE(static_cast<uint16>(poly_flags::Door) == nav::DoorPolyFlag);
}

TEST_CASE("A marked door survives as a separate door-area poly strip", "[nav_build][door]")
{
	const auto mesh = BuildFloor({ Barrier() });

	int doorPolys = 0;
	for (int i = 0; i < mesh->npolys; ++i)
	{
		const Vector3 center = PolyCenter(*mesh, i);
		if ((mesh->areas[i] & poly_flags::Door) != 0)
		{
			++doorPolys;
			CHECK(Barrier().ContainsXZ(center));
		}
		else
		{
			// Floor polys keep their ground area.
			CHECK((mesh->areas[i] & poly_flags::Ground) != 0);
		}
	}

	CHECK(doorPolys > 0);
}

TEST_CASE("Without doors no door polys are produced", "[nav_build][door]")
{
	const auto mesh = BuildFloor({});
	for (int i = 0; i < mesh->npolys; ++i)
	{
		CHECK((mesh->areas[i] & poly_flags::Door) == 0);
	}
}
