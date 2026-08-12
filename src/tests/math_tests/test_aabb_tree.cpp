// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "math/aabb_tree.h"
#include "math/ray.h"

#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"
#include "binary_io/memory_source.h"
#include "binary_io/reader.h"

using namespace mmo;

namespace
{
	// Two horizontal unit quads on the y=0 plane: submesh 0 spans x in [0,1],
	// submesh 1 spans x in [2,3]. Both span z in [0,1].
	AABBTree BuildTwoSubMeshTree()
	{
		const std::vector<AABBTree::Vertex> vertices = {
			// Quad A (submesh 0)
			{ 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f },
			// Quad B (submesh 1)
			{ 2.0f, 0.0f, 0.0f }, { 3.0f, 0.0f, 0.0f }, { 3.0f, 0.0f, 1.0f }, { 2.0f, 0.0f, 1.0f }
		};

		const std::vector<AABBTree::Index> indices = {
			0, 1, 2,  0, 2, 3,
			4, 5, 6,  4, 6, 7
		};

		const std::vector<uint16> faceSubMeshes = { 0, 0, 1, 1 };

		AABBTree tree;
		tree.Build(vertices, indices, faceSubMeshes);
		return tree;
	}

	uint16 SubMeshHitByRayDownAt(const AABBTree& tree, const float x, const float z)
	{
		Ray ray(Vector3(x, 1.0f, z), Vector3(x, -1.0f, z));
		AABBTree::Index faceIndex = 0;
		REQUIRE(tree.IntersectRay(ray, &faceIndex));
		REQUIRE(faceIndex < tree.GetFaceSubMeshes().size());
		return tree.GetFaceSubMeshes()[faceIndex];
	}
}

TEST_CASE("AABBTree maps hit faces to their source submesh", "[aabb_tree]")
{
	const AABBTree tree = BuildTwoSubMeshTree();

	REQUIRE(tree.GetFaceSubMeshes().size() == 4);
	CHECK(SubMeshHitByRayDownAt(tree, 0.5f, 0.5f) == 0);
	CHECK(SubMeshHitByRayDownAt(tree, 2.5f, 0.5f) == 1);
}

TEST_CASE("AABBTree face submesh ids survive serialization round trip", "[aabb_tree]")
{
	const AABBTree tree = BuildTwoSubMeshTree();

	std::vector<char> buffer;
	io::VectorSink sink{ buffer };
	io::Writer writer{ sink };
	writer << tree;

	io::MemorySource source{ buffer };
	io::Reader reader{ source };
	AABBTree loaded;
	reader >> loaded;
	REQUIRE(reader);

	REQUIRE(loaded.GetFaceSubMeshes().size() == 4);
	CHECK(SubMeshHitByRayDownAt(loaded, 0.5f, 0.5f) == 0);
	CHECK(SubMeshHitByRayDownAt(loaded, 2.5f, 0.5f) == 1);
}

TEST_CASE("AABBTree built without submesh ids round trips with empty mapping", "[aabb_tree]")
{
	const std::vector<AABBTree::Vertex> vertices = {
		{ 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 1.0f }
	};
	const std::vector<AABBTree::Index> indices = { 0, 1, 2 };

	AABBTree tree;
	tree.Build(vertices, indices);
	CHECK(tree.GetFaceSubMeshes().empty());

	std::vector<char> buffer;
	io::VectorSink sink{ buffer };
	io::Writer writer{ sink };
	writer << tree;

	io::MemorySource source{ buffer };
	io::Reader reader{ source };
	AABBTree loaded;
	reader >> loaded;
	REQUIRE(reader);
	CHECK(loaded.GetFaceSubMeshes().empty());
}
