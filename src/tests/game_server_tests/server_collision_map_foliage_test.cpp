// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_server/world/server_collision_map.h"
#include "math/aabb_tree.h"
#include "math/matrix4.h"
#include "math/quaternion.h"
#include "math/vector3.h"

#include <memory>
#include <vector>

using namespace mmo;

// Regression tests for bug 6acc8829 ("target not in line of sight although it clearly looked in
// sight"): the solid canopy collision of trees blocked line of sight over them. Foliage now only
// blocks with its trunk; entities keep blocking with all of their geometry.

namespace
{
	/// Appends a two-sided quad in the XY plane (z = 0), so it blocks rays from both sides even
	/// with the back face culling of the static raycast.
	void addQuad(std::vector<AABBTree::Vertex>& verts, std::vector<AABBTree::Index>& indices,
		const float minX, const float maxX, const float minY, const float maxY)
	{
		const AABBTree::Index base = static_cast<AABBTree::Index>(verts.size());
		verts.emplace_back(minX, minY, 0.0f);
		verts.emplace_back(maxX, minY, 0.0f);
		verts.emplace_back(maxX, maxY, 0.0f);
		verts.emplace_back(minX, maxY, 0.0f);

		const AABBTree::Index quad[] = { 0, 1, 2, 2, 1, 0, 0, 2, 3, 3, 2, 0 };
		for (const AABBTree::Index i : quad)
		{
			indices.push_back(base + i);
		}
	}

	std::shared_ptr<AABBTree> buildTree(const std::vector<AABBTree::Vertex>& verts, const std::vector<AABBTree::Index>& indices)
	{
		auto tree = std::make_shared<AABBTree>();
		tree->Build(verts, indices);
		return tree;
	}

	/// A tree shaped like the shipped ones: a thin trunk from the ground to 8 m (crossing the trunk
	/// height cut) and a wide crown from 5 m to 12 m, entirely above the cut.
	std::shared_ptr<AABBTree> buildTreeMesh()
	{
		std::vector<AABBTree::Vertex> verts;
		std::vector<AABBTree::Index> indices;
		addQuad(verts, indices, -0.3f, 0.3f, 0.0f, 8.0f);
		addQuad(verts, indices, -5.0f, 5.0f, 5.0f, 12.0f);
		return buildTree(verts, indices);
	}

	/// Ray along Z at the given x and height, crossing the z = 0 plane of the test geometry.
	bool lineOfSightAt(const ServerCollisionMap& map, const float x, const float y)
	{
		return map.LineOfSight(Vector3(x, y, 10.0f), Vector3(x, y, -10.0f));
	}
}

TEST_CASE("ServerCollisionMap - the trunk height constant sits below the crowns of the shipped trees", "[server_collision_map][foliage]")
{
	// Tree_02's crown starts at about 5 m, and an eye-level ray (1.8 m) must still hit trunks.
	STATIC_REQUIRE(ServerCollisionMap::FoliageLineOfSightTrunkHeight > 1.8f);
	STATIC_REQUIRE(ServerCollisionMap::FoliageLineOfSightTrunkHeight < 5.0f);
}

TEST_CASE("ServerCollisionMap - a foliage crown does not block line of sight", "[server_collision_map][foliage]")
{
	ServerCollisionMap map;
	REQUIRE(map.AddFoliageInstance(buildTreeMesh(), Matrix4::Identity, "tree"));

	// Through the crown beside the trunk: clear.
	CHECK(lineOfSightAt(map, 3.0f, 7.0f));
	CHECK(lineOfSightAt(map, -4.0f, 11.0f));

	Vector3 hitPoint;
	const Vector3 to(3.0f, 7.0f, -10.0f);
	CHECK(map.LineOfSightEx(Vector3(3.0f, 7.0f, 10.0f), to, hitPoint));
	CHECK(hitPoint == to);
}

TEST_CASE("ServerCollisionMap - a foliage trunk still blocks line of sight", "[server_collision_map][foliage]")
{
	ServerCollisionMap map;
	REQUIRE(map.AddFoliageInstance(buildTreeMesh(), Matrix4::Identity, "tree"));

	// Eye level through the trunk.
	CHECK_FALSE(lineOfSightAt(map, 0.0f, 1.8f));

	// A triangle reaching below the cut is kept whole, so the trunk is not shortened.
	CHECK_FALSE(lineOfSightAt(map, 0.0f, 7.0f));

	Vector3 hitPoint;
	REQUIRE_FALSE(map.LineOfSightEx(Vector3(0.0f, 1.8f, 10.0f), Vector3(0.0f, 1.8f, -10.0f), hitPoint));
	CHECK(hitPoint.z == Approx(0.0f).margin(0.01f));
	CHECK(hitPoint.y == Approx(1.8f).margin(0.01f));
}

TEST_CASE("ServerCollisionMap - the same mesh placed as an entity blocks with its crown", "[server_collision_map][foliage]")
{
	ServerCollisionMap map;
	REQUIRE(map.AddInstance(buildTreeMesh(), Matrix4::Identity, "entity"));

	CHECK_FALSE(lineOfSightAt(map, 3.0f, 7.0f));
	CHECK_FALSE(lineOfSightAt(map, 0.0f, 1.8f));
}

TEST_CASE("ServerCollisionMap - the foliage trunk height scales with the instance", "[server_collision_map][foliage]")
{
	ServerCollisionMap map;

	Matrix4 transform;
	transform.MakeTransform(Vector3::Zero, Vector3(2.0f, 2.0f, 2.0f), Quaternion::Identity);
	REQUIRE(map.AddFoliageInstance(buildTreeMesh(), transform, "big tree"));

	// The crown of a doubled tree spans 10 m - 24 m in world space and stays clear.
	CHECK(lineOfSightAt(map, 6.0f, 10.5f));

	// Its trunk is 0.6 m to either side and still blocks at eye level.
	CHECK_FALSE(lineOfSightAt(map, 0.4f, 1.8f));
}

TEST_CASE("ServerCollisionMap - foliage line of sight tree filtering", "[server_collision_map][foliage]")
{
	SECTION("A mesh entirely below the trunk height is shared, not copied")
	{
		std::vector<AABBTree::Vertex> verts;
		std::vector<AABBTree::Index> indices;
		addQuad(verts, indices, -1.0f, 1.0f, 0.0f, 2.0f);
		const auto bush = buildTree(verts, indices);

		CHECK(ServerCollisionMap::MakeFoliageLineOfSightTree(bush) == bush);
	}

	SECTION("A mesh entirely above the trunk height has nothing that blocks")
	{
		std::vector<AABBTree::Vertex> verts;
		std::vector<AABBTree::Index> indices;
		addQuad(verts, indices, -1.0f, 1.0f, 6.0f, 8.0f);
		const auto crown = buildTree(verts, indices);

		CHECK(ServerCollisionMap::MakeFoliageLineOfSightTree(crown) == nullptr);

		ServerCollisionMap map;
		CHECK_FALSE(map.AddFoliageInstance(crown, Matrix4::Identity, "crown"));
		CHECK_FALSE(map.IsLoaded());
	}

	SECTION("Null and empty trees are rejected")
	{
		CHECK(ServerCollisionMap::MakeFoliageLineOfSightTree(nullptr) == nullptr);
		CHECK(ServerCollisionMap::MakeFoliageLineOfSightTree(std::make_shared<AABBTree>()) == nullptr);
	}

	SECTION("The crown is dropped from a tree mesh")
	{
		const auto tree = buildTreeMesh();
		const auto trunk = ServerCollisionMap::MakeFoliageLineOfSightTree(tree);
		REQUIRE(trunk);
		CHECK(trunk != tree);
		CHECK(trunk->GetIndices().size() == tree->GetIndices().size() / 2);
	}
}
