// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_server/world/server_collision_map.h"
#include "math/aabb_tree.h"
#include "math/matrix4.h"
#include "math/quaternion.h"
#include "math/vector3.h"

#include <memory>

using namespace mmo;

namespace
{
	/// Builds a 2x2 m wall quad in the XY plane (facing +Z and -Z via two triangles),
	/// centered on the origin.
	std::shared_ptr<AABBTree> buildWallTree()
	{
		const std::vector<AABBTree::Vertex> verts = {
			Vector3(-1.0f, -1.0f, 0.0f),
			Vector3( 1.0f, -1.0f, 0.0f),
			Vector3( 1.0f,  1.0f, 0.0f),
			Vector3(-1.0f,  1.0f, 0.0f)
		};

		// Two triangles with opposing winding so the quad blocks rays from both sides
		// even with backface culling enabled in the raycast.
		const std::vector<AABBTree::Index> indices = {
			0, 1, 2,
			2, 1, 0,
			0, 2, 3,
			3, 2, 0
		};

		auto tree = std::make_shared<AABBTree>();
		tree->Build(verts, indices);
		return tree;
	}

	// A ray crossing the wall plane at the origin.
	const Vector3 frontPos(0.0f, 0.0f, 2.0f);
	const Vector3 backPos(0.0f, 0.0f, -2.0f);
}

TEST_CASE("ServerCollisionMap - empty map does not block", "[server_collision_map]")
{
	ServerCollisionMap map;
	REQUIRE(map.LineOfSight(frontPos, backPos));
	REQUIRE_FALSE(map.IsLoaded());
}

TEST_CASE("ServerCollisionMap - enabled dynamic instance blocks line of sight", "[server_collision_map]")
{
	ServerCollisionMap map;
	const uint64 handle = map.AddDynamicInstance(buildWallTree(), Matrix4::Identity, true);
	REQUIRE(handle != 0);

	REQUIRE_FALSE(map.LineOfSight(frontPos, backPos));

	// A ray next to the wall passes.
	REQUIRE(map.LineOfSight(frontPos + Vector3(3.0f, 0.0f, 0.0f), backPos + Vector3(3.0f, 0.0f, 0.0f)));
}

TEST_CASE("ServerCollisionMap - disabled instance does not block and can be re-enabled", "[server_collision_map]")
{
	ServerCollisionMap map;
	const uint64 handle = map.AddDynamicInstance(buildWallTree(), Matrix4::Identity, false);
	REQUIRE(handle != 0);

	REQUIRE(map.LineOfSight(frontPos, backPos));

	map.SetDynamicInstanceEnabled(handle, true);
	REQUIRE_FALSE(map.LineOfSight(frontPos, backPos));

	map.SetDynamicInstanceEnabled(handle, false);
	REQUIRE(map.LineOfSight(frontPos, backPos));
}

TEST_CASE("ServerCollisionMap - removed instance does not block", "[server_collision_map]")
{
	ServerCollisionMap map;
	const uint64 handle = map.AddDynamicInstance(buildWallTree(), Matrix4::Identity, true);
	REQUIRE_FALSE(map.LineOfSight(frontPos, backPos));

	map.RemoveDynamicInstance(handle);
	REQUIRE(map.LineOfSight(frontPos, backPos));

	// Unknown/stale handles are a safe no-op.
	map.RemoveDynamicInstance(handle);
	map.RemoveDynamicInstance(12345);
	map.SetDynamicInstanceEnabled(12345, true);
	REQUIRE(map.LineOfSight(frontPos, backPos));
}

TEST_CASE("ServerCollisionMap - null or empty tree yields handle 0", "[server_collision_map]")
{
	ServerCollisionMap map;
	REQUIRE(map.AddDynamicInstance(nullptr, Matrix4::Identity, true) == 0);
	REQUIRE(map.AddDynamicInstance(std::make_shared<AABBTree>(), Matrix4::Identity, true) == 0);
}

TEST_CASE("ServerCollisionMap - instance transform is respected", "[server_collision_map]")
{
	ServerCollisionMap map;

	// Rotate the wall 90 degrees around Y: it now lies in the YZ plane and no longer
	// blocks a ray along the Z axis...
	Matrix4 rotated;
	rotated.MakeTransform(Vector3::Zero, Vector3::UnitScale, Quaternion(Degree(90), Vector3::UnitY));
	const uint64 rotatedHandle = map.AddDynamicInstance(buildWallTree(), rotated, true);
	REQUIRE(rotatedHandle != 0);

	REQUIRE(map.LineOfSight(frontPos, backPos));

	// ...but it blocks a ray along the X axis.
	REQUIRE_FALSE(map.LineOfSight(Vector3(2.0f, 0.0f, 0.0f), Vector3(-2.0f, 0.0f, 0.0f)));

	map.RemoveDynamicInstance(rotatedHandle);

	// A translated wall only blocks rays crossing its new location.
	Matrix4 translated;
	translated.MakeTransform(Vector3(10.0f, 0.0f, 0.0f), Vector3::UnitScale, Quaternion::Identity);
	REQUIRE(map.AddDynamicInstance(buildWallTree(), translated, true) != 0);

	REQUIRE(map.LineOfSight(frontPos, backPos));
	REQUIRE_FALSE(map.LineOfSight(frontPos + Vector3(10.0f, 0.0f, 0.0f), backPos + Vector3(10.0f, 0.0f, 0.0f)));
}

TEST_CASE("ServerCollisionMap - LineOfSightEx reports hit point on the wall plane", "[server_collision_map]")
{
	ServerCollisionMap map;
	REQUIRE(map.AddDynamicInstance(buildWallTree(), Matrix4::Identity, true) != 0);

	Vector3 hitPoint;
	REQUIRE_FALSE(map.LineOfSightEx(frontPos, backPos, hitPoint));

	REQUIRE(hitPoint.x == Approx(0.0f).margin(0.001f));
	REQUIRE(hitPoint.y == Approx(0.0f).margin(0.001f));
	REQUIRE(hitPoint.z == Approx(0.0f).margin(0.001f));

	// Unobstructed query reports the destination.
	Vector3 clearHit;
	REQUIRE(map.LineOfSightEx(frontPos + Vector3(3.0f, 0.0f, 0.0f), backPos + Vector3(3.0f, 0.0f, 0.0f), clearHit));
	REQUIRE(clearHit.x == Approx(backPos.x + 3.0f).margin(0.001f));
	REQUIRE(clearHit.z == Approx(backPos.z).margin(0.001f));
}

TEST_CASE("ServerCollisionMap - identical positions are trivially in sight", "[server_collision_map]")
{
	ServerCollisionMap map;

	// The wall is present so the query cannot take the "no instances" early-out and has to
	// build a ray. Two units standing on the exact same spot can always see each other, and
	// a zero-length ray has no direction to normalize.
	REQUIRE(map.AddDynamicInstance(buildWallTree(), Matrix4::Identity, true) != 0);

	const Vector3 onTheWall(0.0f, 0.0f, 0.0f);
	REQUIRE(map.LineOfSight(frontPos, frontPos));
	REQUIRE(map.LineOfSight(onTheWall, onTheWall));

	Vector3 hitPoint;
	REQUIRE(map.LineOfSightEx(frontPos, frontPos, hitPoint));
	REQUIRE(hitPoint.x == Approx(frontPos.x).margin(0.001f));
	REQUIRE(hitPoint.y == Approx(frontPos.y).margin(0.001f));
	REQUIRE(hitPoint.z == Approx(frontPos.z).margin(0.001f));

	// Sub-millimeter separations are degenerate for the same reason.
	REQUIRE(map.LineOfSight(frontPos, frontPos + Vector3(0.0f, 0.0f, 0.00001f)));
	REQUIRE(map.LineOfSightEx(frontPos, frontPos + Vector3(0.00001f, 0.0f, 0.0f), hitPoint));
}

TEST_CASE("ServerCollisionMap - degenerate threshold is 1 cm and does not swallow real geometry", "[server_collision_map]")
{
	// Pins the threshold from both sides using segments that straddle the wall plane at z = 0.
	// Without the upper case, shrinking the threshold to near-zero would go unnoticed; without
	// the lower case, growing it to a meter would.
	ServerCollisionMap map;
	REQUIRE(map.AddDynamicInstance(buildWallTree(), Matrix4::Identity, true) != 0);

	// 9 mm apart, straddling the wall: below the threshold, so reported as in sight.
	const Vector3 nearFront(0.0f, 0.0f, 0.0045f);
	const Vector3 nearBack(0.0f, 0.0f, -0.0045f);
	REQUIRE(map.LineOfSight(nearFront, nearBack));

	Vector3 hitPoint;
	REQUIRE(map.LineOfSightEx(nearFront, nearBack, hitPoint));
	REQUIRE(hitPoint.z == Approx(nearBack.z).margin(0.0001f));

	// 2 cm apart, straddling the same wall: above the threshold, so actually traced and blocked.
	const Vector3 farFront(0.0f, 0.0f, 0.01f);
	const Vector3 farBack(0.0f, 0.0f, -0.01f);
	REQUIRE_FALSE(map.LineOfSight(farFront, farBack));

	REQUIRE_FALSE(map.LineOfSightEx(farFront, farBack, hitPoint));
	REQUIRE(hitPoint.z == Approx(0.0f).margin(0.001f));
}

TEST_CASE("ServerCollisionMap - degenerate transform is rejected instead of corrupting queries", "[server_collision_map]")
{
	// A zero component in the authored scale makes the transform singular, so its inverse
	// would be filled with inf/NaN. Every ray transformed into that instance's local space
	// would come out NaN, and NaN comparisons silently slip past the degeneracy guards —
	// the instance would then block or not block at random. Such instances must never be
	// registered in the first place.
	ServerCollisionMap map;

	// A well-formed wall well off the query line, so the queries below cannot take the
	// "no instances at all" early-out and have to build a ray and walk the instance list.
	Matrix4 offToTheSide;
	offToTheSide.MakeTransform(Vector3(10.0f, 0.0f, 0.0f), Vector3::UnitScale, Quaternion::Identity);
	REQUIRE(map.AddDynamicInstance(buildWallTree(), offToTheSide, true) != 0);

	Matrix4 flattenedZ;
	flattenedZ.MakeTransform(Vector3::Zero, Vector3(1.0f, 1.0f, 0.0f), Quaternion::Identity);
	REQUIRE(map.AddDynamicInstance(buildWallTree(), flattenedZ, true) == 0);

	Matrix4 flattenedX;
	flattenedX.MakeTransform(Vector3::Zero, Vector3(0.0f, 1.0f, 1.0f), Quaternion(Degree(30), Vector3::UnitY));
	REQUIRE(map.AddDynamicInstance(buildWallTree(), flattenedX, true) == 0);

	Matrix4 collapsed;
	collapsed.MakeTransform(Vector3(1.0f, 2.0f, 3.0f), Vector3::Zero, Quaternion::Identity);
	REQUIRE(map.AddDynamicInstance(buildWallTree(), collapsed, true) == 0);

	// None of the three was registered, so the traversal only sees the wall off to the side
	// and answers normally instead of returning a NaN-poisoned verdict.
	REQUIRE(map.LineOfSight(frontPos, backPos));

	Vector3 hitPoint;
	REQUIRE(map.LineOfSightEx(frontPos, backPos, hitPoint));
	REQUIRE(hitPoint.x == Approx(backPos.x).margin(0.001f));
	REQUIRE(hitPoint.y == Approx(backPos.y).margin(0.001f));
	REQUIRE(hitPoint.z == Approx(backPos.z).margin(0.001f));

	// A well-formed instance added afterwards still works — rejection is per instance.
	REQUIRE(map.AddDynamicInstance(buildWallTree(), Matrix4::Identity, true) != 0);
	REQUIRE_FALSE(map.LineOfSight(frontPos, backPos));
	REQUIRE_FALSE(map.LineOfSightEx(frontPos, backPos, hitPoint));
	REQUIRE(hitPoint.z == Approx(0.0f).margin(0.001f));
}

TEST_CASE("ServerCollisionMap - tiny but invertible scales are still accepted", "[server_collision_map]")
{
	// The rejection above must key on invertibility, not on "small" — a legitimately
	// shrunken prop still has to collide.
	ServerCollisionMap map;

	Matrix4 shrunk;
	shrunk.MakeTransform(Vector3::Zero, Vector3(0.01f, 0.01f, 0.01f), Quaternion::Identity);
	REQUIRE(map.AddDynamicInstance(buildWallTree(), shrunk, true) != 0);

	// The wall is now 2 cm across, so a ray through the origin still crosses it.
	REQUIRE_FALSE(map.LineOfSight(frontPos, backPos));

	// Four more orders of magnitude down the determinant is 1e-12, and still invertible.
	// This pins the criterion against anyone later swapping it for a scale-magnitude epsilon.
	Matrix4 minuscule;
	minuscule.MakeTransform(Vector3::Zero, Vector3(0.0001f, 0.0001f, 0.0001f), Quaternion::Identity);
	REQUIRE(map.AddDynamicInstance(buildWallTree(), minuscule, true) != 0);
}

TEST_CASE("ServerCollisionMap - instances sharing one tree are independent", "[server_collision_map]")
{
	ServerCollisionMap map;
	const auto sharedTree = buildWallTree();

	Matrix4 translated;
	translated.MakeTransform(Vector3(10.0f, 0.0f, 0.0f), Vector3::UnitScale, Quaternion::Identity);

	const uint64 first = map.AddDynamicInstance(sharedTree, Matrix4::Identity, true);
	const uint64 second = map.AddDynamicInstance(sharedTree, translated, true);
	REQUIRE(first != 0);
	REQUIRE(second != 0);
	REQUIRE(first != second);

	// Removing the first instance must not affect the second (the tree is shared).
	map.RemoveDynamicInstance(first);
	REQUIRE(map.LineOfSight(frontPos, backPos));
	REQUIRE_FALSE(map.LineOfSight(frontPos + Vector3(10.0f, 0.0f, 0.0f), backPos + Vector3(10.0f, 0.0f, 0.0f)));
}
