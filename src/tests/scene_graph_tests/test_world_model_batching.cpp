// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "base/typedefs.h"

#include "math/matrix4.h"
#include "math/quaternion.h"
#include "math/vector3.h"
#include "math/vector4.h"
#include "scene_graph/world_model_batch_builder.h"

#include <cmath>
#include <map>
#include <vector>

using namespace mmo;

namespace
{
	/// Builds a mesh-facts lookup over a fixed table, so bucketing can be exercised without loading
	/// a single asset. Unknown paths resolve to nullptr, which is what a failed load looks like.
	WorldModelMeshLookup MakeLookup(const std::map<String, WorldModelMeshFacts>& table)
	{
		return [&table](const String& path) -> const WorldModelMeshFacts*
		{
			const auto it = table.find(path);
			return it == table.end() ? nullptr : &it->second;
		};
	}

	WorldModelPlacementInput MakePlacement(
		const size_t groupIndex,
		const size_t sourceIndex,
		const String& meshPath,
		const String& materialOverride = "")
	{
		WorldModelPlacementInput placement;
		placement.groupIndex = groupIndex;
		placement.sourceIndex = sourceIndex;
		placement.meshPath = meshPath;
		placement.materialOverride = materialOverride;
		return placement;
	}

	/// A mesh with one submesh that can be instanced - the common dungeon module.
	WorldModelMeshFacts SimpleMesh(const uint16 submeshCount = 1)
	{
		WorldModelMeshFacts facts;
		facts.submeshCount = submeshCount;
		facts.batchable = true;
		return facts;
	}
}

TEST_CASE("Repeated placements of one mesh in one room form a single bucket", "[world_model_batching]")
{
	const std::map<String, WorldModelMeshFacts> meshes{ { "Floor_01.hmsh", SimpleMesh() } };

	const std::vector<WorldModelPlacementInput> placements{
		MakePlacement(0, 0, "Floor_01.hmsh"),
		MakePlacement(0, 1, "Floor_01.hmsh"),
		MakePlacement(0, 2, "Floor_01.hmsh")
	};

	std::vector<WorldModelBucket> buckets;
	std::vector<WorldModelPlacementInput> singletons;
	BuildWorldModelBuckets(placements, MakeLookup(meshes), 2, buckets, singletons);

	REQUIRE(buckets.size() == 1);
	REQUIRE(buckets[0].placements.size() == 3);
	REQUIRE(buckets[0].key.groupIndex == 0);
	REQUIRE(buckets[0].key.submeshIndex == 0);
	REQUIRE(singletons.empty());
}

TEST_CASE("The same mesh in two rooms produces two buckets", "[world_model_batching]")
{
	// Room is part of the batch key because portal culling toggles visibility per room. Merging
	// across rooms would make a single batch visible whenever either room was.
	const std::map<String, WorldModelMeshFacts> meshes{ { "Wall.hmsh", SimpleMesh() } };

	const std::vector<WorldModelPlacementInput> placements{
		MakePlacement(0, 0, "Wall.hmsh"),
		MakePlacement(0, 1, "Wall.hmsh"),
		MakePlacement(1, 2, "Wall.hmsh"),
		MakePlacement(1, 3, "Wall.hmsh")
	};

	std::vector<WorldModelBucket> buckets;
	std::vector<WorldModelPlacementInput> singletons;
	BuildWorldModelBuckets(placements, MakeLookup(meshes), 2, buckets, singletons);

	REQUIRE(buckets.size() == 2);
	REQUIRE(buckets[0].key.groupIndex == 0);
	REQUIRE(buckets[1].key.groupIndex == 1);
	REQUIRE(buckets[0].placements.size() == 2);
	REQUIRE(buckets[1].placements.size() == 2);
	REQUIRE(singletons.empty());
}

TEST_CASE("Differing material overrides split a bucket", "[world_model_batching]")
{
	// A material override replaces the material of every submesh, so an overridden placement cannot
	// share a draw call with a non-overridden one.
	const std::map<String, WorldModelMeshFacts> meshes{ { "Column.hmsh", SimpleMesh() } };

	const std::vector<WorldModelPlacementInput> placements{
		MakePlacement(0, 0, "Column.hmsh"),
		MakePlacement(0, 1, "Column.hmsh"),
		MakePlacement(0, 2, "Column.hmsh", "Marble.hmat"),
		MakePlacement(0, 3, "Column.hmsh", "Marble.hmat")
	};

	std::vector<WorldModelBucket> buckets;
	std::vector<WorldModelPlacementInput> singletons;
	BuildWorldModelBuckets(placements, MakeLookup(meshes), 2, buckets, singletons);

	REQUIRE(buckets.size() == 2);
	REQUIRE(singletons.empty());

	// Key order sorts the empty override before "Marble.hmat".
	REQUIRE(buckets[0].key.materialOverride.empty());
	REQUIRE(buckets[1].key.materialOverride == "Marble.hmat");
}

TEST_CASE("A multi-submesh mesh produces one bucket per submesh", "[world_model_batching]")
{
	const std::map<String, WorldModelMeshFacts> meshes{ { "Arch.hmsh", SimpleMesh(3) } };

	const std::vector<WorldModelPlacementInput> placements{
		MakePlacement(0, 0, "Arch.hmsh"),
		MakePlacement(0, 1, "Arch.hmsh")
	};

	std::vector<WorldModelBucket> buckets;
	std::vector<WorldModelPlacementInput> singletons;
	BuildWorldModelBuckets(placements, MakeLookup(meshes), 2, buckets, singletons);

	REQUIRE(buckets.size() == 3);
	for (uint16 i = 0; i < 3; ++i)
	{
		REQUIRE(buckets[i].key.submeshIndex == i);
		REQUIRE(buckets[i].placements.size() == 2);
	}
	REQUIRE(singletons.empty());
}

TEST_CASE("A lone placement is demoted to the per-entity path", "[world_model_batching]")
{
	// One instanced draw of one instance is still one draw call, so batching it would buy nothing
	// while adding an instance buffer and a dependency on the material's instanced shader variant.
	const std::map<String, WorldModelMeshFacts> meshes{ { "Statue.hmsh", SimpleMesh() } };

	const std::vector<WorldModelPlacementInput> placements{ MakePlacement(2, 7, "Statue.hmsh") };

	std::vector<WorldModelBucket> buckets;
	std::vector<WorldModelPlacementInput> singletons;
	BuildWorldModelBuckets(placements, MakeLookup(meshes), 2, buckets, singletons);

	REQUIRE(buckets.empty());
	REQUIRE(singletons.size() == 1);
	REQUIRE(singletons[0].sourceIndex == 7);
	REQUIRE(singletons[0].groupIndex == 2);
}

TEST_CASE("A lone placement of a multi-submesh mesh is demoted exactly once", "[world_model_batching]")
{
	// Regression guard: the per-entity path draws all submeshes of a mesh together, so demoting a
	// placement once per submesh bucket would create duplicate entities stacked on each other.
	const std::map<String, WorldModelMeshFacts> meshes{ { "Fountain.hmsh", SimpleMesh(4) } };

	const std::vector<WorldModelPlacementInput> placements{ MakePlacement(0, 0, "Fountain.hmsh") };

	std::vector<WorldModelBucket> buckets;
	std::vector<WorldModelPlacementInput> singletons;
	BuildWorldModelBuckets(placements, MakeLookup(meshes), 2, buckets, singletons);

	REQUIRE(buckets.empty());
	REQUIRE(singletons.size() == 1);
}

TEST_CASE("Skinned meshes never reach the instanced path", "[world_model_batching]")
{
	// The device selects the instanced vertex shader unconditionally once an instance buffer is
	// bound, so a skinned mesh would be transformed by a shader with no bone data at all.
	WorldModelMeshFacts skinned;
	skinned.submeshCount = 1;
	skinned.batchable = false;

	const std::map<String, WorldModelMeshFacts> meshes{ { "Banner_Animated.hmsh", skinned } };

	const std::vector<WorldModelPlacementInput> placements{
		MakePlacement(0, 0, "Banner_Animated.hmsh"),
		MakePlacement(0, 1, "Banner_Animated.hmsh"),
		MakePlacement(0, 2, "Banner_Animated.hmsh")
	};

	std::vector<WorldModelBucket> buckets;
	std::vector<WorldModelPlacementInput> singletons;
	BuildWorldModelBuckets(placements, MakeLookup(meshes), 2, buckets, singletons);

	REQUIRE(buckets.empty());
	REQUIRE(singletons.size() == 3);
}

TEST_CASE("Placements whose mesh cannot be resolved fall back to the per-entity path", "[world_model_batching]")
{
	const std::map<String, WorldModelMeshFacts> meshes{ { "Known.hmsh", SimpleMesh() } };

	const std::vector<WorldModelPlacementInput> placements{
		MakePlacement(0, 0, "Missing.hmsh"),
		MakePlacement(0, 1, "Missing.hmsh")
	};

	std::vector<WorldModelBucket> buckets;
	std::vector<WorldModelPlacementInput> singletons;
	BuildWorldModelBuckets(placements, MakeLookup(meshes), 2, buckets, singletons);

	REQUIRE(buckets.empty());
	REQUIRE(singletons.size() == 2);
}

TEST_CASE("Placements outside any room keep their own bucket", "[world_model_batching]")
{
	// Doodads that lie in no room must never be portal-culled, so they must not be merged into a
	// room's batch.
	const std::map<String, WorldModelMeshFacts> meshes{ { "Torch.hmsh", SimpleMesh() } };

	const std::vector<WorldModelPlacementInput> placements{
		MakePlacement(0, 0, "Torch.hmsh"),
		MakePlacement(0, 1, "Torch.hmsh"),
		MakePlacement(WorldModelNoGroup, 2, "Torch.hmsh"),
		MakePlacement(WorldModelNoGroup, 3, "Torch.hmsh")
	};

	std::vector<WorldModelBucket> buckets;
	std::vector<WorldModelPlacementInput> singletons;
	BuildWorldModelBuckets(placements, MakeLookup(meshes), 2, buckets, singletons);

	REQUIRE(buckets.size() == 2);
	REQUIRE(singletons.empty());

	// WorldModelNoGroup is (size_t)-1, so it sorts last.
	REQUIRE(buckets[0].key.groupIndex == 0);
	REQUIRE(buckets[1].key.groupIndex == WorldModelNoGroup);
}

TEST_CASE("Bucket order is deterministic", "[world_model_batching]")
{
	const std::map<String, WorldModelMeshFacts> meshes{
		{ "A.hmsh", SimpleMesh() },
		{ "B.hmsh", SimpleMesh() }
	};

	// Feed the placements in an order that does not match key order.
	const std::vector<WorldModelPlacementInput> placements{
		MakePlacement(1, 0, "B.hmsh"),
		MakePlacement(0, 1, "B.hmsh"),
		MakePlacement(1, 2, "A.hmsh"),
		MakePlacement(0, 3, "A.hmsh"),
		MakePlacement(1, 4, "B.hmsh"),
		MakePlacement(0, 5, "B.hmsh"),
		MakePlacement(1, 6, "A.hmsh"),
		MakePlacement(0, 7, "A.hmsh")
	};

	std::vector<WorldModelBucket> buckets;
	std::vector<WorldModelPlacementInput> singletons;
	BuildWorldModelBuckets(placements, MakeLookup(meshes), 2, buckets, singletons);

	REQUIRE(buckets.size() == 4);
	REQUIRE(buckets[0].key.groupIndex == 0);
	REQUIRE(buckets[0].key.meshPath == "A.hmsh");
	REQUIRE(buckets[1].key.groupIndex == 0);
	REQUIRE(buckets[1].key.meshPath == "B.hmsh");
	REQUIRE(buckets[2].key.groupIndex == 1);
	REQUIRE(buckets[2].key.meshPath == "A.hmsh");
	REQUIRE(buckets[3].key.groupIndex == 1);
	REQUIRE(buckets[3].key.meshPath == "B.hmsh");
}

TEST_CASE("Placement order within a bucket is preserved", "[world_model_batching]")
{
	const std::map<String, WorldModelMeshFacts> meshes{ { "Floor.hmsh", SimpleMesh() } };

	const std::vector<WorldModelPlacementInput> placements{
		MakePlacement(0, 10, "Floor.hmsh"),
		MakePlacement(0, 20, "Floor.hmsh"),
		MakePlacement(0, 30, "Floor.hmsh")
	};

	std::vector<WorldModelBucket> buckets;
	std::vector<WorldModelPlacementInput> singletons;
	BuildWorldModelBuckets(placements, MakeLookup(meshes), 2, buckets, singletons);

	REQUIRE(buckets.size() == 1);
	REQUIRE(buckets[0].placements[0].sourceIndex == 10);
	REQUIRE(buckets[0].placements[1].sourceIndex == 20);
	REQUIRE(buckets[0].placements[2].sourceIndex == 30);
}

TEST_CASE("Composed world transform matches the scene graph under a uniform parent", "[world_model_batching]")
{
	// The baseline: with a uniform parent scale, composing component-wise and multiplying the two
	// matrices agree, so a batch and an entity land in exactly the same place.
	WorldModelPlacementInput placement;
	placement.position = Vector3(3.0f, 4.0f, 5.0f);
	placement.rotation = Quaternion(Degree(35.0f), Vector3::UnitY);
	placement.scale = Vector3(2.0f, 2.0f, 2.0f);

	const Vector3 parentPosition(10.0f, 0.0f, -7.0f);
	const Quaternion parentOrientation(Degree(90.0f), Vector3::UnitY);
	const Vector3 parentScale(3.0f, 3.0f, 3.0f);

	Matrix4 parentMatrix;
	parentMatrix.MakeTransform(parentPosition, parentScale, parentOrientation);
	Matrix4 localMatrix;
	localMatrix.MakeTransform(placement.position, placement.scale, placement.rotation);

	const Matrix4 composed = ComposeWorldTransform(parentPosition, parentOrientation, parentScale, placement);
	const Matrix4 multiplied = parentMatrix * localMatrix;

	const Vector3 probe(1.0f, 2.0f, 3.0f);
	const Vector3 a = composed * probe;
	const Vector3 b = multiplied * probe;

	REQUIRE(a.x == Approx(b.x).margin(0.001f));
	REQUIRE(a.y == Approx(b.y).margin(0.001f));
	REQUIRE(a.z == Approx(b.z).margin(0.001f));
}

TEST_CASE("Composed world transform follows the scene graph under a non-uniform parent", "[world_model_batching]")
{
	// The case that motivates ComposeWorldTransform. Node::UpdateFromParentImpl multiplies scales
	// element-wise and orientations as quaternions; a plain matrix product shears instead once the
	// parent scale is non-uniform and the child is rotated. A batch built the matrix way would then
	// no longer line up with the neighbouring placements that batching demoted to entities.
	WorldModelPlacementInput placement;
	placement.position = Vector3(3.0f, 0.0f, 0.0f);
	placement.rotation = Quaternion(Degree(45.0f), Vector3::UnitY);
	placement.scale = Vector3::UnitScale;

	const Vector3 parentPosition = Vector3::Zero;
	const Quaternion parentOrientation = Quaternion::Identity;
	const Vector3 parentScale(4.0f, 1.0f, 1.0f);   // non-uniform

	const Matrix4 composed = ComposeWorldTransform(parentPosition, parentOrientation, parentScale, placement);

	// Position still follows the scene graph rule: parentOrientation * (parentScale * localPos).
	const Vector3 origin = composed * Vector3::Zero;
	REQUIRE(origin.x == Approx(12.0f).margin(0.001f));
	REQUIRE(origin.y == Approx(0.0f).margin(0.001f));
	REQUIRE(origin.z == Approx(0.0f).margin(0.001f));

	// And it differs from the naive matrix product, which is the whole point.
	Matrix4 parentMatrix;
	parentMatrix.MakeTransform(parentPosition, parentScale, parentOrientation);
	Matrix4 localMatrix;
	localMatrix.MakeTransform(placement.position, placement.scale, placement.rotation);
	const Matrix4 multiplied = parentMatrix * localMatrix;

	const Vector3 probe(0.0f, 0.0f, 1.0f);
	const Vector3 a = composed * probe;
	const Vector3 b = multiplied * probe;
	REQUIRE(std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.z - b.z) > 0.01f);
}

TEST_CASE("A null mesh lookup demotes everything instead of crashing", "[world_model_batching]")
{
	const std::vector<WorldModelPlacementInput> placements{
		MakePlacement(0, 0, "Floor.hmsh"),
		MakePlacement(0, 1, "Floor.hmsh")
	};

	std::vector<WorldModelBucket> buckets;
	std::vector<WorldModelPlacementInput> singletons;
	BuildWorldModelBuckets(placements, WorldModelMeshLookup(), 2, buckets, singletons);

	REQUIRE(buckets.empty());
	REQUIRE(singletons.size() == 2);
}
