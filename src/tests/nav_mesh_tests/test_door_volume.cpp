// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "synthetic_nav_map.h"

#include "nav_mesh/door_volume.h"
#include "nav_mesh/instance_query_filter.h"
#include "nav_mesh/map.h"
#include "math/aabb.h"
#include "math/degree.h"
#include "math/quaternion.h"

#include <cmath>

using namespace mmo;
using namespace test_nav;

namespace
{
	/// The magic barrier's collision: a 6 m wide, 7 m tall plane, zero thickness. In mesh space
	/// it spans z, so the +90 degree entity yaw turns it to span x.
	const AABB BarrierBounds(Vector3(0.f, 0.f, -3.f), Vector3(0.f, 7.f, 3.f));
}

TEST_CASE("MakeDoorTransform applies the +90 degree entity yaw", "[nav_mesh][door_volume]")
{
	const Matrix4 transform = nav::MakeDoorTransform(Vector3(10.f, 2.f, 20.f), Quaternion::Identity, 1.f);
	const Vector3 worldPoint = transform * Vector3(0.f, 0.f, 3.f);

	// +90 degrees around Y turns local Z onto world X (the sign depends on the engine's
	// quaternion handedness, which the server's LOS transform already relies on).
	CHECK(std::abs(worldPoint.x - 10.f) == Approx(3.f).margin(0.001f));
	CHECK(worldPoint.y == Approx(2.f).margin(0.001f));
	CHECK(worldPoint.z == Approx(20.f).margin(0.001f));
}

TEST_CASE("ComputeDoorVolume inflates a zero-thickness barrier", "[nav_mesh][door_volume]")
{
	const Matrix4 transform = nav::MakeDoorTransform(Vector3(10.f, 2.f, 20.f), Quaternion::Identity, 1.f);
	const nav::DoorVolume volume = nav::ComputeDoorVolume(BarrierBounds, transform);

	// 6 m wide along world x, 0 m thick along world z, both grown by the agent radius.
	Vector3 min, max;
	volume.GetWorldBounds(min, max);
	CHECK(max.x - min.x == Approx(6.f + 2.f * nav::DoorVolumeInflate).margin(0.01f));
	CHECK(max.z - min.z == Approx(2.f * nav::DoorVolumeInflate).margin(0.01f));
	CHECK(volume.minY == Approx(2.f - nav::DoorVolumeHeightPadding).margin(0.01f));
	CHECK(volume.maxY == Approx(9.f + nav::DoorVolumeHeightPadding).margin(0.01f));

	CHECK(volume.ContainsXZ(Vector3(10.f, 0.f, 20.f)));
	CHECK(volume.ContainsXZ(Vector3(13.4f, 0.f, 20.4f)));
	CHECK_FALSE(volume.ContainsXZ(Vector3(10.f, 0.f, 20.6f)));
	CHECK_FALSE(volume.ContainsXZ(Vector3(13.6f, 0.f, 20.f)));
}

TEST_CASE("ComputeDoorVolume follows the spawn yaw and scale", "[nav_mesh][door_volume]")
{
	// A further 90 degree spawn yaw turns the barrier back to span z. Scale 2 doubles it.
	const Quaternion yaw(Degree(90), Vector3::UnitY);
	const Matrix4 transform = nav::MakeDoorTransform(Vector3::Zero, yaw, 2.f);
	const nav::DoorVolume volume = nav::ComputeDoorVolume(BarrierBounds, transform);

	Vector3 min, max;
	volume.GetWorldBounds(min, max);
	CHECK(max.z - min.z == Approx(12.f + 2.f * nav::DoorVolumeInflate).margin(0.01f));
	CHECK(max.x - min.x == Approx(2.f * nav::DoorVolumeInflate).margin(0.01f));
}

TEST_CASE("FindDoorPolys returns only door polys inside the volume", "[nav_mesh][door_volume]")
{
	// Row of three quads: [floor | door | floor]. Both floor quads are also inside the query box.
	const auto tiles = [](const int32 x, const int32 y) -> std::optional<std::vector<uint8>>
	{
		if (x != 512 || y != 512)
		{
			return std::nullopt;
		}

		return BuildGridTile(x, y, 3, 1, [](const int32 col, int32)
		{
			return static_cast<uint16>(col == 1 ? (1 | nav::DoorPolyFlag) : 1);
		}, false);
	};
	SyntheticNavMap files({ { 32, 32 } }, {}, tiles);
	nav::Map map(SyntheticNavMap::Name);
	REQUIRE(map.LoadPage(32, 32));

	const Vector3 origin = TileOrigin(512, 512);
	nav::DoorVolume volume;
	volume.center = origin + Vector3(TileSize * 0.5f, 0.f, TileSize * 0.5f);
	volume.axisX = Vector3::UnitX;
	volume.axisZ = Vector3::UnitZ;
	volume.halfX = TileSize * 0.5f;
	volume.halfZ = TileSize * 0.5f;
	volume.minY = -1.f;
	volume.maxY = 1.f;

	const std::vector<dtPolyRef> refs = map.FindDoorPolys(volume);
	REQUIRE(refs.size() == 1);

	unsigned short flags = 0;
	REQUIRE(dtStatusSucceed(map.GetNavMesh().getPolyFlags(refs[0], &flags)));
	CHECK((flags & nav::DoorPolyFlag) != 0);

	// A volume that does not reach the door quad's center finds nothing.
	volume.halfX = TileSize / 6.f - 0.5f;
	volume.center = origin + Vector3(TileSize / 6.f, 0.f, TileSize * 0.5f);
	CHECK(map.FindDoorPolys(volume).empty());
}
