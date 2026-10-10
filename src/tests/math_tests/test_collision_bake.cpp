// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "base/typedefs.h"
#include "math/collision_bake.h"

using namespace mmo;

namespace
{
	// Two unit quads on y = 0: submesh 0 at x 0..1, submesh 1 at x 2..3.
	const std::vector<Vector3> s_vertices = {
		{ 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 }, { 0, 0, 1 },
		{ 2, 0, 0 }, { 3, 0, 0 }, { 3, 0, 1 }, { 2, 0, 1 }
	};
	const std::vector<uint32> s_indices = { 0, 2, 1, 0, 3, 2, 4, 6, 5, 4, 7, 6 };
	const std::vector<uint16> s_subMeshes = { 0, 0, 1, 1 };

	CollisionShape Box(const Vector3& position, const Vector3& scale, collision_shape_op::Type op, uint16 surface = 0)
	{
		CollisionShape shape;
		shape.type = collision_shape_type::Box;
		shape.op = op;
		shape.position = position;
		shape.scale = scale;
		shape.surfaceSubMesh = surface;
		return shape;
	}
}

TEST_CASE("Bake without shapes keeps the render geometry", "[collision_bake]")
{
	const CollisionBakeResult result = BakeCollision(s_vertices, s_indices, s_subMeshes, {});
	CHECK(result.indices.size() == s_indices.size());
	CHECK(result.vertices.size() == 8);
	CHECK(result.faceSubMeshes == s_subMeshes);
	CHECK(result.faceShape == std::vector<int32>{ -1, -1, -1, -1 });
	CHECK(result.cutFaces == 0);
}

TEST_CASE("Cut shape removes the faces whose centroid it contains and compacts vertices", "[collision_bake]")
{
	const auto cut = Box(Vector3(2.5f, 0.0f, 0.5f), Vector3(2.0f, 1.0f, 2.0f), collision_shape_op::Cut);
	const CollisionBakeResult result = BakeCollision(s_vertices, s_indices, s_subMeshes, { cut });

	CHECK(result.cutFaces == 2);
	CHECK(result.cutFaceIndices == std::vector<uint32>{ 2, 3 });
	CHECK(result.indices.size() == 6);
	CHECK(result.vertices.size() == 4);
	CHECK(result.faceSubMeshes == std::vector<uint16>{ 0, 0 });
	for (const auto& v : result.vertices)
	{
		CHECK(v.x <= 1.0f);
	}
}

TEST_CASE("Add shape appends its triangles with its surface submesh", "[collision_bake]")
{
	const auto add = Box(Vector3(5.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f), collision_shape_op::Add, 7);
	const CollisionBakeResult result = BakeCollision(s_vertices, s_indices, s_subMeshes, { add });

	REQUIRE(result.indices.size() == s_indices.size() + 36);
	CHECK(result.faceSubMeshes.size() == 16);
	CHECK(result.faceSubMeshes.back() == 7);
	CHECK(result.faceShape.back() == 0);
	CHECK(result.faceShape.front() == -1);
	for (const uint32 i : result.indices)
	{
		CHECK(i < result.vertices.size());
	}
}

TEST_CASE("Cut shapes never remove faces of added shapes", "[collision_bake]")
{
	const auto add = Box(Vector3(5.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f), collision_shape_op::Add);
	const auto cut = Box(Vector3(5.0f, 0.0f, 0.0f), Vector3(3.0f, 3.0f, 3.0f), collision_shape_op::Cut);
	const CollisionBakeResult result = BakeCollision(s_vertices, s_indices, s_subMeshes, { add, cut });
	CHECK(result.indices.size() == s_indices.size() + 36);
	CHECK(result.cutFaces == 0);
}

TEST_CASE("Bake with nothing left is empty", "[collision_bake]")
{
	CHECK(BakeCollision({}, {}, {}, {}).indices.empty());

	const auto cutAll = Box(Vector3(1.5f, 0.0f, 0.5f), Vector3(10.0f, 1.0f, 10.0f), collision_shape_op::Cut);
	const CollisionBakeResult result = BakeCollision(s_vertices, s_indices, s_subMeshes, { cutAll });
	CHECK(result.indices.empty());
	CHECK(result.vertices.empty());
	CHECK(result.cutFaces == 4);
}

TEST_CASE("Bake without a submesh mapping uses submesh 0 and skips out-of-range faces", "[collision_bake]")
{
	std::vector<uint32> indices = s_indices;
	indices.push_back(0);
	indices.push_back(1);
	indices.push_back(99);     // broken face
	const CollisionBakeResult result = BakeCollision(s_vertices, indices, {}, {});
	CHECK(result.indices.size() == s_indices.size());
	CHECK(result.faceSubMeshes == std::vector<uint16>{ 0, 0, 0, 0 });
}
