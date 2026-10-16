// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "math/degree.h"
#include "math/quaternion.h"
#include "math/radian.h"
#include "scene_graph/world_model_merge.h"

#include <vector>

using namespace mmo;

namespace
{
	/// One triangle in the XY plane facing +Z, with tangent +X and binormal +Y.
	std::vector<WorldModelMergeVertex> Triangle()
	{
		std::vector<WorldModelMergeVertex> v(3);
		v[0].pos = Vector3(0.0f, 0.0f, 0.0f);
		v[1].pos = Vector3(1.0f, 0.0f, 0.0f);
		v[2].pos = Vector3(0.0f, 1.0f, 0.0f);
		for (auto& vertex : v)
		{
			vertex.normal = Vector3::UnitZ;
			vertex.tangent = Vector3::UnitX;
			vertex.binormal = Vector3::UnitY;
			vertex.color = 0xFFFFFFFF;
		}
		v[1].uv[0] = 1.0f;
		v[2].uv[1] = 1.0f;
		return v;
	}

	const std::vector<uint32> TriangleIndices{ 0, 1, 2 };

	void CheckVector(const Vector3& actual, const Vector3& expected)
	{
		CHECK(actual.x == Approx(expected.x).margin(1e-5f));
		CHECK(actual.y == Approx(expected.y).margin(1e-5f));
		CHECK(actual.z == Approx(expected.z).margin(1e-5f));
	}
}

TEST_CASE("Merged geometry is moved into world model space", "[world_model_merge]")
{
	const auto tri = Triangle();
	Matrix4 transform;
	transform.MakeTransform(Vector3(10.0f, 2.0f, -3.0f), Vector3(2.0f, 2.0f, 2.0f), Quaternion(Radian(Degree(90.0f)), Vector3::UnitY));

	std::vector<WorldModelMergeVertex> vertices;
	std::vector<uint32> indices;
	AppendTransformedGeometry(tri.data(), tri.size(), TriangleIndices.data(), TriangleIndices.size(), transform, vertices, indices);

	REQUIRE(vertices.size() == 3);
	CheckVector(vertices[0].pos, Vector3(10.0f, 2.0f, -3.0f));
	CheckVector(vertices[1].pos, transform * Vector3(1.0f, 0.0f, 0.0f));

	// Rotated 90 degrees about Y: +Z normal turns to +X, +X tangent to -Z; lengths stay one
	CheckVector(vertices[0].normal, Vector3::UnitX);
	CheckVector(vertices[0].tangent, Vector3::NegativeUnitZ);
	CheckVector(vertices[0].binormal, Vector3::UnitY);

	// Texture coordinates and colour pass through untouched
	CHECK(vertices[1].uv[0] == 1.0f);
	CHECK(vertices[2].uv[1] == 1.0f);
	CHECK(vertices[0].color == 0xFFFFFFFF);
	CHECK(indices == std::vector<uint32>{ 0, 1, 2 });
}

TEST_CASE("Merged normals follow non-uniform scale correctly", "[world_model_merge]")
{
	// A slanted face: normal (1,1,0)/sqrt2. Stretching X by 4 must tilt the normal towards Y,
	// which only the inverse transpose gets right (the plain matrix would tilt it towards X).
	std::vector<WorldModelMergeVertex> tri = Triangle();
	for (auto& v : tri)
	{
		v.normal = Vector3(1.0f, 1.0f, 0.0f).NormalizedCopy();
	}

	Matrix4 transform;
	transform.MakeTransform(Vector3::Zero, Vector3(4.0f, 1.0f, 1.0f), Quaternion::Identity);
	std::vector<WorldModelMergeVertex> vertices;
	std::vector<uint32> indices;
	AppendTransformedGeometry(tri.data(), tri.size(), TriangleIndices.data(), TriangleIndices.size(), transform, vertices, indices);

	CheckVector(vertices[0].normal, Vector3(0.25f, 1.0f, 0.0f).NormalizedCopy());
}

TEST_CASE("Merged mirrored geometry is re-wound", "[world_model_merge]")
{
	const auto tri = Triangle();
	Matrix4 mirror;
	mirror.MakeTransform(Vector3::Zero, Vector3(-1.0f, 1.0f, 1.0f), Quaternion::Identity);

	std::vector<WorldModelMergeVertex> vertices;
	std::vector<uint32> indices;
	AppendTransformedGeometry(tri.data(), tri.size(), TriangleIndices.data(), TriangleIndices.size(), mirror, vertices, indices);

	// The winding of the mirrored triangle, seen along its normal, must match the original's
	const Vector3 a = vertices[indices[0]].pos;
	const Vector3 b = vertices[indices[1]].pos;
	const Vector3 c = vertices[indices[2]].pos;
	const Vector3 faceNormal = (b - a).Cross(c - a);
	CHECK(faceNormal.Dot(vertices[0].normal) > 0.0f);

	// Mirroring X leaves a +Z normal alone and flips the +X tangent
	CheckVector(vertices[0].normal, Vector3::UnitZ);
	CheckVector(vertices[0].tangent, Vector3::NegativeUnitX);
}

TEST_CASE("Merged indices are offset past earlier geometry", "[world_model_merge]")
{
	const auto tri = Triangle();
	std::vector<WorldModelMergeVertex> vertices;
	std::vector<uint32> indices;
	AppendTransformedGeometry(tri.data(), tri.size(), TriangleIndices.data(), TriangleIndices.size(), Matrix4::Identity, vertices, indices);
	AppendTransformedGeometry(tri.data(), tri.size(), TriangleIndices.data(), TriangleIndices.size(), Matrix4::Identity, vertices, indices);

	CHECK(vertices.size() == 6);
	CHECK(indices == std::vector<uint32>{ 0, 1, 2, 3, 4, 5 });
}

TEST_CASE("Small placements are detail", "[world_model_merge]")
{
	CHECK(IsWorldModelDetail(0.4f));
	CHECK(IsWorldModelDetail(WorldModelDetailRadius));
	CHECK_FALSE(IsWorldModelDetail(4.0f));
}
