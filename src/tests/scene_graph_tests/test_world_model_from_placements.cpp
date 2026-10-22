// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "scene_graph/world_model.h"
#include "scene_graph/world_model_from_placements.h"

using namespace mmo;

namespace
{
	WorldModelPlacementSource MakePlacement(const String& mesh, const Vector3& position, const float scale = 1.0f)
	{
		WorldModelPlacementSource placement;
		placement.meshPath = mesh;
		placement.position = position;
		placement.scale = Vector3(scale, scale, scale);
		placement.meshBounds = AABB(Vector3(-1.0f, 0.0f, -1.0f), Vector3(1.0f, 2.0f, 1.0f));
		return placement;
	}
}

TEST_CASE("Placements become exterior grid cell groups relative to the origin", "[world_model][placements]")
{
	const Vector3 origin(100.0f, 10.0f, 200.0f);
	std::vector<WorldModelPlacementSource> placements;
	placements.push_back(MakePlacement("Models/A.hmsh", Vector3(105.0f, 12.0f, 205.0f)));
	placements.push_back(MakePlacement("Models/B.hmsh", Vector3(140.0f, 10.0f, 201.0f), 2.0f));
	placements.push_back(MakePlacement("Models/C.hmsh", Vector3(110.0f, 10.0f, 199.0f)));
	placements.back().materialOverride = "Models/Override.hmi";

	WorldModel model;
	REQUIRE(AddWorldModelGroupsFromPlacements(model, placements, origin, 32.0f) == 3);
	REQUIRE(model.GetGroupCount() == 3);

	// Ordered by cell row (z) then column (x): (0,-1), (0,0), (1,0)
	const WorldModelGroup* north = model.GetGroup(0);
	REQUIRE(north->GetName() == "Cell 0,-1");
	REQUIRE(north->IsExterior());
	REQUIRE(north->GetMeshRefs().size() == 1);
	CHECK(north->GetMeshRefs()[0].meshPath == "Models/C.hmsh");
	CHECK(north->GetMeshRefs()[0].materialOverride == "Models/Override.hmi");
	CHECK(north->GetMeshRefs()[0].position.x == Approx(10.0f));
	CHECK(north->GetMeshRefs()[0].position.z == Approx(-1.0f));

	const WorldModelGroup* first = model.GetGroup(1);
	REQUIRE(first->GetName() == "Cell 0,0");
	REQUIRE(first->GetMeshRefs().size() == 1);
	CHECK(first->GetMeshRefs()[0].position.y == Approx(2.0f));

	// Group bounds cover the transformed mesh bounds (scale 2 around (40, 0, 1))
	const WorldModelGroup* east = model.GetGroup(2);
	REQUIRE(east->GetName() == "Cell 1,0");
	CHECK(east->GetBoundingBox().min.x == Approx(38.0f));
	CHECK(east->GetBoundingBox().max.x == Approx(42.0f));
	CHECK(east->GetBoundingBox().max.y == Approx(4.0f));

	CHECK(model.GetBoundingBox().min.z == Approx(-2.0f));
	CHECK(model.GetBoundingBox().max.x == Approx(42.0f));
}

TEST_CASE("No placements add no groups", "[world_model][placements]")
{
	WorldModel model;
	CHECK(AddWorldModelGroupsFromPlacements(model, {}, Vector3::Zero, 50.0f) == 0);
	CHECK(model.GetGroupCount() == 0);
}
