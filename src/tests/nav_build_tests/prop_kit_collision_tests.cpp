// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "nav_build/map.h"
#include "scene_graph/prop_kit.h"
#include "math/aabb_tree.h"
#include "math/matrix4.h"

#include <map>

using namespace mmo;

namespace
{
	/// Builds the collision tree of an axis aligned unit cube centred on the origin.
	AABBTree MakeUnitCubeTree()
	{
		const std::vector<Vector3> vertices = {
			Vector3(-0.5f, -0.5f, -0.5f), Vector3(0.5f, -0.5f, -0.5f), Vector3(0.5f, 0.5f, -0.5f), Vector3(-0.5f, 0.5f, -0.5f),
			Vector3(-0.5f, -0.5f, 0.5f), Vector3(0.5f, -0.5f, 0.5f), Vector3(0.5f, 0.5f, 0.5f), Vector3(-0.5f, 0.5f, 0.5f)
		};

		const std::vector<uint32> indices = {
			0, 1, 2, 0, 2, 3,
			4, 6, 5, 4, 7, 6,
			0, 4, 5, 0, 5, 1,
			3, 2, 6, 3, 6, 7,
			0, 3, 7, 0, 7, 4,
			1, 5, 6, 1, 6, 2
		};

		AABBTree tree;
		tree.Build(vertices, indices);
		return tree;
	}

	WorldModelMeshRef MakeProp(const String& meshPath, const Vector3& position)
	{
		WorldModelMeshRef prop;
		prop.meshPath = meshPath;
		prop.position = position;
		return prop;
	}

	Matrix4 MakeTranslation(const Vector3& position)
	{
		Matrix4 transform;
		transform.MakeTransform(position, Vector3(1.0f, 1.0f, 1.0f), Quaternion::Identity);
		return transform;
	}
}

TEST_CASE("Kit props add their collision triangles at the placement transform", "[nav_build][prop_kit]")
{
	const AABBTree cube = MakeUnitCubeTree();
	const auto collisionOf = [&cube](const String& meshPath) -> const AABBTree*
	{
		return meshPath == "Models/Crate.hmsh" ? &cube : nullptr;
	};

	PropKit kit;
	kit.props.push_back(MakeProp("Models/Crate.hmsh", Vector3(2.0f, 0.0f, 0.0f)));

	// Geometry already in the buffers must stay untouched and the new indices must point past it
	std::vector<Vector3> vertices = { Vector3(0.0f, 0.0f, 0.0f) };
	std::vector<int32> indices = { 0, 0, 0 };

	AppendPropKitTriangles(kit, MakeTranslation(Vector3(100.0f, 0.0f, 0.0f)), collisionOf, vertices, indices);

	REQUIRE(vertices.size() == 1 + cube.GetVertices().size());
	REQUIRE(indices.size() == 3 + cube.GetIndices().size());

	for (size_t i = 1; i < vertices.size(); ++i)
	{
		CHECK(vertices[i].x >= Approx(101.5f));
		CHECK(vertices[i].x <= Approx(102.5f));
		CHECK(vertices[i].y >= Approx(-0.5f));
		CHECK(vertices[i].y <= Approx(0.5f));
	}

	for (size_t i = 3; i < indices.size(); ++i)
	{
		CHECK(indices[i] >= 1);
		CHECK(indices[i] < static_cast<int32>(vertices.size()));
	}
}

TEST_CASE("Invisible and collision-less kit props add nothing", "[nav_build][prop_kit]")
{
	const AABBTree cube = MakeUnitCubeTree();
	const AABBTree empty;
	const auto collisionOf = [&cube, &empty](const String& meshPath) -> const AABBTree*
	{
		if (meshPath == "Models/Crate.hmsh")
		{
			return &cube;
		}

		if (meshPath == "Models/Rug.hmsh")
		{
			return &empty;
		}

		return nullptr;
	};

	PropKit kit;

	WorldModelMeshRef hidden = MakeProp("Models/Crate.hmsh", Vector3(2.0f, 0.0f, 0.0f));
	hidden.visible = false;
	kit.props.push_back(hidden);

	kit.props.push_back(MakeProp("Models/Rug.hmsh", Vector3(0.0f, 0.0f, 0.0f)));
	kit.props.push_back(MakeProp("Models/Missing.hmsh", Vector3(0.0f, 0.0f, 0.0f)));
	kit.props.push_back(MakeProp("", Vector3(0.0f, 0.0f, 0.0f)));

	std::vector<Vector3> vertices;
	std::vector<int32> indices;

	AppendPropKitTriangles(kit, MakeTranslation(Vector3(100.0f, 0.0f, 0.0f)), collisionOf, vertices, indices);

	CHECK(vertices.empty());
	CHECK(indices.empty());
}

TEST_CASE("Kit props apply their own rotation and scale before the placement", "[nav_build][prop_kit]")
{
	const AABBTree cube = MakeUnitCubeTree();
	const auto collisionOf = [&cube](const String&) -> const AABBTree*
	{
		return &cube;
	};

	PropKit kit;
	WorldModelMeshRef prop = MakeProp("Models/Crate.hmsh", Vector3(0.0f, 0.0f, 4.0f));
	prop.scale = Vector3(2.0f, 2.0f, 2.0f);
	kit.props.push_back(prop);

	// A placement rotated 90 degrees around Y maps the prop's +Z offset onto +X
	Matrix4 placement;
	placement.MakeTransform(Vector3(10.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f), Quaternion(Radian(1.5707963f), Vector3::UnitY));

	std::vector<Vector3> vertices;
	std::vector<int32> indices;
	AppendPropKitTriangles(kit, placement, collisionOf, vertices, indices);

	REQUIRE(!vertices.empty());
	for (const Vector3& v : vertices)
	{
		CHECK(v.x >= Approx(13.0f).margin(0.001f));
		CHECK(v.x <= Approx(15.0f).margin(0.001f));
		CHECK(v.z >= Approx(-1.0f).margin(0.001f));
		CHECK(v.z <= Approx(1.0f).margin(0.001f));
	}
}
