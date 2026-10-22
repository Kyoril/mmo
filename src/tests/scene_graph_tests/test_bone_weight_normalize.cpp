// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "null_device.h"
#include "graphics/vertex_index_data.h"
#include "scene_graph/mesh.h"
#include "scene_graph/sub_mesh.h"

#include <algorithm>
#include <vector>

using namespace mmo;

namespace
{
	SubMesh& CreateSkinnedSubMesh(Mesh& mesh, const size_t vertexCount)
	{
		SubMesh& subMesh = mesh.CreateSubMesh();
		subMesh.useSharedVertices = false;
		subMesh.vertexData = std::make_unique<VertexData>();
		subMesh.vertexData->vertexCount = vertexCount;
		subMesh.vertexData->vertexDeclaration->AddElement(0, 0, VertexElementType::Float3, VertexElementSemantic::Position);
		return subMesh;
	}

	void Add(SubMesh& subMesh, const uint32 vertex, const uint16 bone, const float weight)
	{
		subMesh.AddBoneAssignment(VertexBoneAssignment{ vertex, bone, weight });
	}
}

TEST_CASE("Compiling bone assignments sorts them by vertex and keeps per-vertex order", "[bone_weights]")
{
	test::EnsureNullDevice();
	Mesh mesh("Test.hmsh");
	SubMesh& subMesh = CreateSkinnedSubMesh(mesh, 3);

	// Unsorted, as an importer may add them.
	Add(subMesh, 2, 7, 1.0f);
	Add(subMesh, 0, 3, 0.25f);
	Add(subMesh, 1, 5, 1.0f);
	Add(subMesh, 0, 1, 0.75f);

	subMesh.CompileBoneAssignments();

	const auto& assignments = subMesh.GetBoneAssignments();
	REQUIRE(assignments.size() == 4);
	CHECK(std::is_sorted(assignments.begin(), assignments.end(), [](const auto& a, const auto& b) { return a.first < b.first; }));
	CHECK(assignments[0].second.boneIndex == 3);
	CHECK(assignments[1].second.boneIndex == 1);
	CHECK(assignments[2].second.boneIndex == 5);
	CHECK(assignments[3].second.boneIndex == 7);
}

TEST_CASE("Compiling bone assignments trims a vertex to its four strongest weights and renormalizes", "[bone_weights]")
{
	test::EnsureNullDevice();
	Mesh mesh("Test.hmsh");
	SubMesh& subMesh = CreateSkinnedSubMesh(mesh, 2);

	Add(subMesh, 0, 1, 0.3f);
	Add(subMesh, 0, 2, 0.05f);
	Add(subMesh, 0, 3, 0.2f);
	Add(subMesh, 0, 4, 0.05f);
	Add(subMesh, 0, 5, 0.2f);
	Add(subMesh, 0, 6, 0.1f);
	Add(subMesh, 1, 9, 0.5f);

	subMesh.CompileBoneAssignments();

	const auto& assignments = subMesh.GetBoneAssignments();
	REQUIRE(assignments.size() == 5);

	// The two lowest weights (bones 2 and 4) are gone; the rest keep their order.
	std::vector<uint16> bones;
	float total = 0.0f;
	for (size_t i = 0; i < 4; ++i)
	{
		CHECK(assignments[i].first == 0);
		bones.push_back(assignments[i].second.boneIndex);
		total += assignments[i].second.weight;
	}
	CHECK(bones == std::vector<uint16>{ 1, 3, 5, 6 });
	CHECK(total == Approx(1.0f));
	CHECK(assignments[0].second.weight == Approx(0.3f / 0.8f));

	// A lone weight is normalized too.
	CHECK(assignments[4].first == 1);
	CHECK(assignments[4].second.weight == Approx(1.0f));
}

TEST_CASE("Trimming bone weights drops the earlier of two equal lowest weights", "[bone_weights]")
{
	test::EnsureNullDevice();
	Mesh mesh("Test.hmsh");
	SubMesh& subMesh = CreateSkinnedSubMesh(mesh, 1);

	Add(subMesh, 0, 1, 0.1f);
	Add(subMesh, 0, 2, 0.1f);
	Add(subMesh, 0, 3, 0.3f);
	Add(subMesh, 0, 4, 0.3f);
	Add(subMesh, 0, 5, 0.2f);

	subMesh.CompileBoneAssignments();

	const auto& assignments = subMesh.GetBoneAssignments();
	REQUIRE(assignments.size() == 4);
	CHECK(assignments[0].second.boneIndex == 2);
	CHECK(assignments[1].second.boneIndex == 3);
	CHECK(assignments[2].second.boneIndex == 4);
	CHECK(assignments[3].second.boneIndex == 5);
}
