// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "scene_graph/portal.h"
#include "scene_graph/prop_kit.h"
#include "scene_graph/prop_kit_checks.h"
#include "scene_graph/world_model.h"

using namespace mmo;

namespace
{
	/// One portal in the plane x = 10, 2 m wide along Z around z = 0 and 4 m high.
	void addDoorway(WorldModel& model)
	{
		Portal& portal = model.AddPortal();
		portal.SetTransform(Vector3(10.0f, 2.0f, 0.0f), Quaternion(Radian(Pi * 0.5f), Vector3::UnitY), Vector3::UnitScale);
		portal.SetDimensions(2.0f, 4.0f);
	}

	PropKit makeKit(const Vector3& position)
	{
		WorldModelMeshRef prop;
		prop.meshPath = "Props/Crate.hmsh";
		prop.name = "Crate";
		prop.position = position;

		PropKit kit;
		kit.props.push_back(prop);
		return kit;
	}

	/// A 1 m box around the mesh origin.
	std::optional<AABB> unitBox(const String&)
	{
		return AABB(Vector3(-0.5f, 0.0f, -0.5f), Vector3(0.5f, 1.0f, 0.5f));
	}
}

TEST_CASE("A prop in a doorway is reported", "[prop_kit]")
{
	WorldModel model;
	addDoorway(model);

	const auto issues = FindPropsNearPortals(model, makeKit(Vector3(10.0f, 0.0f, 0.0f)), unitBox);
	REQUIRE(issues.size() == 1);
	CHECK(issues[0].portalIndex == 0);
	CHECK(issues[0].propName == "Crate");
	CHECK(issues[0].distance == Approx(0.0f));
}

TEST_CASE("A prop 1 m away from every portal is not reported", "[prop_kit]")
{
	WorldModel model;
	addDoorway(model);

	// Box spans x 8.0..9.0, so the gap to the portal plane at x = 10 is 1 m
	const auto issues = FindPropsNearPortals(model, makeKit(Vector3(8.5f, 0.0f, 0.0f)), unitBox);
	CHECK(issues.empty());
}

TEST_CASE("Props without collision are ignored", "[prop_kit]")
{
	WorldModel model;
	addDoorway(model);

	const auto noCollision = [](const String&) -> std::optional<AABB> { return std::nullopt; };
	const auto issues = FindPropsNearPortals(model, makeKit(Vector3(10.0f, 0.0f, 0.0f)), noCollision);
	CHECK(issues.empty());
}

namespace
{
	WorldModelMeshRef namedRef(const String& name)
	{
		WorldModelMeshRef ref;
		ref.meshPath = "Props/Crate.hmsh";
		ref.name = name;
		return ref;
	}
}

TEST_CASE("Named mesh refs are removed from every group", "[prop_kit]")
{
	WorldModel model;
	model.AddGroup().GetMeshRefs() = { namedRef("Table"), namedRef("Wall"), namedRef("Chair") };
	model.AddGroup().GetMeshRefs() = { namedRef("Chair"), namedRef("Beam") };

	const MeshRefRemoval removal = RemoveMeshRefsByName(model, { "Table", "Chair" });

	CHECK(removal.removed == 3);
	CHECK(removal.unmatchedNames.empty());
	REQUIRE(model.GetGroup(0)->GetMeshRefs().size() == 1);
	CHECK(model.GetGroup(0)->GetMeshRefs()[0].name == "Wall");
	REQUIRE(model.GetGroup(1)->GetMeshRefs().size() == 1);
	CHECK(model.GetGroup(1)->GetMeshRefs()[0].name == "Beam");
}

TEST_CASE("A re-run of a mesh ref removal removes nothing and names every miss", "[prop_kit]")
{
	WorldModel model;
	model.AddGroup().GetMeshRefs() = { namedRef("Wall") };

	const MeshRefRemoval removal = RemoveMeshRefsByName(model, { "Chair", "Table" });

	CHECK(removal.removed == 0);
	CHECK(removal.unmatchedNames == std::vector<String>{ "Chair", "Table" });
	CHECK(model.GetGroup(0)->GetMeshRefs().size() == 1);
}
