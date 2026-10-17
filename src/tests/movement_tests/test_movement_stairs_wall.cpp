// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "test_helpers/test_scene.h"
#include "test_helpers/test_net_client.h"
#include "test_helpers/test_game_unit.h"
#include "test_helpers/triangle_collidable.h"
#include "test_helpers/simulate_movement.h"
#include "client_data/project.h"
#include "math/vector3.h"
#include "math/capsule.h"
#include "game/movement_type.h"

#include <cmath>

namespace mmo
{
	namespace
	{
		void AddFloor(TriangleCollidable& floor)
		{
			floor.AddTriangle(Vector3(-50.0f, 0.0f, -50.0f), Vector3(50.0f, 0.0f, -50.0f), Vector3(-50.0f, 0.0f, 50.0f));
			floor.AddTriangle(Vector3(50.0f, 0.0f, -50.0f), Vector3(50.0f, 0.0f, 50.0f), Vector3(-50.0f, 0.0f, 50.0f));
		}

		/// Vertical face spanning x in [xMin,xMax], y in [yBottom,yTop] at the given z, normal facing -Z.
		void AddRiser(TriangleCollidable& out, float xMin, float xMax, float yBottom, float yTop, float z)
		{
			out.AddTriangle(Vector3(xMin, yBottom, z), Vector3(xMax, yTop, z), Vector3(xMax, yBottom, z));
			out.AddTriangle(Vector3(xMin, yBottom, z), Vector3(xMin, yTop, z), Vector3(xMax, yTop, z));
		}

		Capsule MakeStandardCapsule(const Vector3& pos)
		{
			const float radius = 0.35f;
			const float halfHeight = 0.9f;
			return Capsule(pos + Vector3(0.0f, radius, 0.0f), pos + Vector3(0.0f, radius + halfHeight * 2.0f, 0.0f), radius);
		}
	}

	TEST_CASE("wedged in geometry: unit can walk back out of a wall it is penetrating", "[movement][wall][penetration]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		TriangleCollidable floor;
		AddFloor(floor);

		constexpr float wallX = 0.8f;
		constexpr float riserZ = 3.0f;

		// Side wall at x = wallX, normal facing -X.
		TriangleCollidable wall;
		wall.AddTriangle(Vector3(wallX, 0.0f, -10.0f), Vector3(wallX, 0.0f, 20.0f), Vector3(wallX, 10.0f, -10.0f));
		wall.AddTriangle(Vector3(wallX, 0.0f, 20.0f), Vector3(wallX, 10.0f, 20.0f), Vector3(wallX, 10.0f, -10.0f));

		// Tall step face at z = riserZ, normal facing -Z, forming a crease with the side wall.
		TriangleCollidable riser;
		AddRiser(riser, -10.0f, wallX, 0.0f, 10.0f, riserZ);

		scene.SetCollidables({ &floor, &wall, &riser });

		// Start the capsule overlapping both surfaces, as happens when the player is squeezed
		// into the crease between a stair step and the wall beside it.
		const Vector3 startPos(0.7f, 0.0f, 2.9f);
		auto unit = std::make_shared<TestGameUnit>(scene, net, project, 0);
		const float run = 7.0f;
		unit->SetSpeed(movement_type::Run, run);
		unit->SetSpeed(movement_type::Walk, run);
		unit->SetSpeed(movement_type::Backwards, run * 0.5f);
		unit->SetColliderForTest(MakeStandardCapsule(startPos));

		SceneNode* node = unit->GetSceneNode();
		if (node) { node->SetPosition(startPos); }

		// Walk away from the crease for two seconds.
		SimulateMovement(*unit, Vector3(-1.0f, 0.0f, -1.0f).NormalizedCopy(), 120, 1.0f / 60.0f);

		const Vector3 pos = unit->GetPosition();
		INFO("pos = (" << pos.x << ", " << pos.y << ", " << pos.z << ")");

		REQUIRE(!std::isnan(pos.x));
		REQUIRE(!std::isnan(pos.z));

		// The unit must have left the geometry it started inside of, not stayed wedged in place.
		CHECK(pos.x < startPos.x - 1.0f);
		CHECK(pos.z < startPos.z - 1.0f);
	}
}
