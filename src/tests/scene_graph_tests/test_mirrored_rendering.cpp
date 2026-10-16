// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "graphics/graphics_device.h"
#include "math/matrix4.h"
#include "math/quaternion.h"
#include "math/vector3.h"
#include "scene_graph/mesh_instance_data.h"
#include "scene_graph/world_model_batch.h"
#include "scene_graph/world_model_batch_builder.h"

#include <map>
#include <vector>

using namespace mmo;

// A transform with an odd number of negative scale axes mirrors the mesh, which reverses the winding
// of every triangle. One-sided materials then cull the side that faces the viewer and the mesh looks
// inside out (stairs and panels the town placed mirrored, as the source map did).

TEST_CASE("Mirrored geometry culls the opposite face", "[mirrored_rendering]")
{
	CHECK(GetMaterialCullMode(false, false) == FaceCullMode::Front);
	CHECK(GetMaterialCullMode(false, true) == FaceCullMode::Back);
	CHECK(GetMaterialCullMode(true, false) == FaceCullMode::None);
	CHECK(GetMaterialCullMode(true, true) == FaceCullMode::None);
}

TEST_CASE("Mirrored placements never share an instanced batch with unmirrored ones", "[mirrored_rendering]")
{
	WorldModelMeshFacts facts;
	facts.submeshCount = 1;
	facts.batchable = true;
	const std::map<String, WorldModelMeshFacts> meshes{ { "Stairs_02.hmsh", facts } };
	const WorldModelMeshLookup lookup = [&meshes](const String& path) -> const WorldModelMeshFacts*
	{
		const auto it = meshes.find(path);
		return it == meshes.end() ? nullptr : &it->second;
	};

	std::vector<WorldModelPlacementInput> placements;
	for (size_t i = 0; i < 4; ++i)
	{
		WorldModelPlacementInput placement;
		placement.groupIndex = 0;
		placement.sourceIndex = i;
		placement.meshPath = "Stairs_02.hmsh";
		placement.scale = (i % 2 == 0) ? Vector3(1.0f, 1.0f, 1.0f) : Vector3(-1.0f, 1.0f, 1.0f);
		placements.push_back(placement);
	}

	std::vector<WorldModelBucket> buckets;
	std::vector<WorldModelPlacementInput> singletons;
	BuildWorldModelBuckets(placements, lookup, 2, buckets, singletons);

	REQUIRE(buckets.size() == 2);
	CHECK(buckets[0].key.mirrored != buckets[1].key.mirrored);
	for (const auto& bucket : buckets)
	{
		REQUIRE(bucket.placements.size() == 2);
		for (const auto& placement : bucket.placements)
		{
			const bool mirrored = placement.scale.x * placement.scale.y * placement.scale.z < 0.0f;
			CHECK(mirrored == bucket.key.mirrored);
		}
	}
}

TEST_CASE("Two negative axes cancel out and are not mirrored", "[mirrored_rendering]")
{
	WorldModelPlacementInput placement;
	placement.scale = Vector3(-1.0f, -1.0f, 1.0f);
	CHECK_FALSE(IsMirroredPlacement(placement));

	placement.scale = Vector3(-1.0f, -1.0f, -1.0f);
	CHECK(IsMirroredPlacement(placement));
}

TEST_CASE("An instanced batch reports whether its instances are mirrored", "[mirrored_rendering]")
{
	WorldModelBatch batch("batch", nullptr, 0, nullptr);
	CHECK_FALSE(batch.IsMirrored());

	MeshInstanceData instance;
	instance.worldMatrix.MakeTransform(Vector3(1.0f, 2.0f, 3.0f), Vector3(-1.0f, 1.0f, 1.0f), Quaternion::Identity);
	batch.AddInstance(instance);
	CHECK(batch.IsMirrored());

	batch.ClearInstances();
	instance.worldMatrix.MakeTransform(Vector3(1.0f, 2.0f, 3.0f), Vector3(2.0f, 2.0f, 2.0f), Quaternion::Identity);
	batch.AddInstance(instance);
	CHECK_FALSE(batch.IsMirrored());
}
