// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

// math/ray.h uses int32 without pulling the typedefs in itself, so this has to come first.
#include "base/typedefs.h"

#include "math/aabb.h"
#include "math/matrix4.h"
#include "math/quaternion.h"
#include "math/ray.h"
#include "math/vector3.h"
#include "scene_graph/instanced_mesh_collision.h"
#include "scene_graph/mesh.h"

#include <cmath>
#include <vector>

using namespace mmo;

namespace
{
	/// Builds a unit cube (centered on the origin, half extent 1) with a collision tree, which is
	/// the minimum a foliage collision proxy needs: AddInstance reads the bounds and both query
	/// paths walk the collision tree.
	MeshPtr MakeCollidableCubeMesh()
	{
		auto mesh = std::make_shared<Mesh>("DegenerateFoliageTestCube");
		mesh->SetBounds(AABB(Vector3(-1.0f, -1.0f, -1.0f), Vector3(1.0f, 1.0f, 1.0f)));

		const std::vector<AABBTree::Vertex> vertices{
			Vector3(-1.0f, -1.0f, -1.0f), Vector3(1.0f, -1.0f, -1.0f),
			Vector3(1.0f,  1.0f, -1.0f), Vector3(-1.0f,  1.0f, -1.0f),
			Vector3(-1.0f, -1.0f,  1.0f), Vector3(1.0f, -1.0f,  1.0f),
			Vector3(1.0f,  1.0f,  1.0f), Vector3(-1.0f,  1.0f,  1.0f)
		};

		const std::vector<AABBTree::Index> indices{
			0, 1, 2,  0, 2, 3,   // -Z
			4, 6, 5,  4, 7, 6,   // +Z
			0, 4, 5,  0, 5, 1,   // -Y
			3, 2, 6,  3, 6, 7,   // +Y
			0, 3, 7,  0, 7, 4,   // -X
			1, 5, 6,  1, 6, 2    // +X
		};

		mesh->GetCollisionTree().Build(vertices, indices);
		return mesh;
	}

	/// A transform with the given scale, no rotation, placed at the origin — exactly the shape
	/// InstancedFoliage::BuildTransform produces from authored .hfol data.
	Matrix4 MakeFoliageTransform(const Vector3& scale)
	{
		Matrix4 transform;
		transform.MakeTransform(Vector3::Zero, scale, Quaternion::Identity);
		return transform;
	}

	bool IsFiniteVector(const Vector3& value)
	{
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}
}

// Authored .hfol data can carry a zero scale component. The resulting transform is singular, and
// Matrix4::Inverse() has no singularity check: on Windows it hands back DirectXMath's all-QNaN
// matrix, on every other platform it trips an assert. A stored NaN inverse is worse than a missing
// instance, because NaN compares false against everything and so silently corrupts every ray
// transformed into that instance's local space rather than announcing itself.

TEST_CASE("InstancedMeshCollision rejects an instance whose transform is not invertible", "[instanced_mesh_collision]")
{
	InstancedMeshCollision collision("DegenerateScaleFoliage", MakeCollidableCubeMesh());

	// One flattened axis is enough to make the transform singular.
	collision.AddInstance(MakeFoliageTransform(Vector3(1.0f, 1.0f, 0.0f)));
	collision.AddInstance(MakeFoliageTransform(Vector3(0.0f, 1.0f, 1.0f)));
	collision.AddInstance(MakeFoliageTransform(Vector3::Zero));

	REQUIRE(collision.GetInstanceCount() == 0);
	REQUIRE_FALSE(collision.IsCollidable());
}

TEST_CASE("InstancedMeshCollision keeps legitimately shrunken instances", "[instanced_mesh_collision]")
{
	// The criterion is invertibility, not the magnitude of the scale. A prop authored at 1/1000th
	// size has a determinant of 1e-9 — far below the FLT_EPSILON tolerance any determinant-vs-zero
	// test would use — yet it inverts cleanly and must still collide.
	InstancedMeshCollision collision("TinyScaleFoliage", MakeCollidableCubeMesh());
	collision.AddInstance(MakeFoliageTransform(Vector3(0.001f, 0.001f, 0.001f)));
	collision.Finalize();

	REQUIRE(collision.GetInstanceCount() == 1);
	REQUIRE(collision.IsCollidable());

	// The ray path is the one that consumes the stored inverse transform.
	const Ray ray(Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, -1.0f, 0.0f));

	CollisionResult result;
	REQUIRE(collision.TestRayCollision(ray, result));
	REQUIRE(IsFiniteVector(result.contactPoint));
	REQUIRE(IsFiniteVector(result.contactNormal));
	REQUIRE(std::isfinite(result.distance));
}

TEST_CASE("InstancedMeshCollision skips degenerate instances without dropping their neighbours", "[instanced_mesh_collision]")
{
	// A single bad instance in a cell must not cost the cell its other foliage.
	InstancedMeshCollision collision("MixedScaleFoliage", MakeCollidableCubeMesh());
	collision.AddInstance(MakeFoliageTransform(Vector3(1.0f, 0.0f, 1.0f)));
	collision.AddInstance(MakeFoliageTransform(Vector3(1.0f, 1.0f, 1.0f)));
	collision.Finalize();

	REQUIRE(collision.GetInstanceCount() == 1);

	const Ray ray(Vector3(0.0f, 5.0f, 0.0f), Vector3(0.0f, -5.0f, 0.0f));

	CollisionResult result;
	REQUIRE(collision.TestRayCollision(ray, result));
	REQUIRE(IsFiniteVector(result.contactPoint));
	REQUIRE(IsFiniteVector(result.contactNormal));
	REQUIRE(std::isfinite(result.distance));
}
