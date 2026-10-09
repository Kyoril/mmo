// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

// math/ray.h uses int32 without pulling the typedefs in itself, so this has to come first.
#include "base/typedefs.h"

#include "binary_io/memory_source.h"
#include "binary_io/reader.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"
#include "math/aabb.h"
#include "math/capsule.h"
#include "math/ray.h"
#include "math/vector3.h"
#include "scene_graph/entity.h"
#include "scene_graph/mesh.h"

#include <vector>

using namespace mmo;

namespace
{
	/// Builds a mesh whose collision tree holds one triangle in the y=0 plane, but whose root is
	/// an inner node pointing back at itself (children == 0) with the triangle's leaf as node 1.
	/// No builder produces this; it stands for any malformed tree loaded from disk. A traversal
	/// that pushes node.children without checking it moves forward pops the root forever.
	MeshPtr MakeMeshWithSelfReferencingRoot()
	{
		std::vector<char> buffer;
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };

		const AABB bounds(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 1.0f));
		writer
			<< io::write<uint32>('BVH1')
			<< io::write<uint32>(3) << Vector3(0.0f, 0.0f, 0.0f) << Vector3(1.0f, 0.0f, 0.0f) << Vector3(1.0f, 0.0f, 1.0f)
			<< io::write<uint32>(3) << io::write<uint32>(0) << io::write<uint32>(1) << io::write<uint32>(2)
			<< io::write<uint32>(2)
			<< io::write<uint8>(0) << io::write<uint32>(0) << bounds	// inner root, children = itself
			<< io::write<uint8>(1) << io::write<uint32>(0) << bounds	// leaf holding face 0
			<< io::write<uint32>('FOOB');

		auto mesh = std::make_shared<Mesh>("SelfReferencingCollisionTree");
		io::MemorySource source{ buffer };
		io::Reader reader{ source };
		reader >> mesh->GetCollisionTree();
		REQUIRE(reader);
		REQUIRE_FALSE(mesh->GetCollisionTree().IsEmpty());
		return mesh;
	}
}

TEST_CASE("Entity ray test terminates on a collision tree whose root points back at itself", "[entity_collision]")
{
	const Entity entity("SelfReferencingEntity", MakeMeshWithSelfReferencingRoot());
	REQUIRE(entity.IsCollidable());

	const Ray ray(Vector3(0.75f, 1.0f, 0.25f), Vector3(0.75f, -1.0f, 0.25f));
	CollisionResult result;
	entity.TestRayCollision(ray, result);

	// Reaching this line is the assertion: before the fix the call never returned.
	SUCCEED();
}

TEST_CASE("Entity capsule test terminates on a collision tree whose root points back at itself", "[entity_collision]")
{
	const Entity entity("SelfReferencingEntity", MakeMeshWithSelfReferencingRoot());
	REQUIRE(entity.IsCollidable());

	const Capsule capsule(Vector3(0.75f, 0.5f, 0.25f), Vector3(0.75f, 1.5f, 0.25f), 0.6f);
	std::vector<CollisionResult> results;
	entity.TestCapsuleCollision(capsule, results);

	SUCCEED();
}
