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
	TEST_CASE("flat floor walk - unit advances forward and stays on floor", "[movement]")
	{
		// Arrange
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		// Build flat floor: two large triangles at Y=0 covering [-50,50] in XZ
		TriangleCollidable floor;
		floor.AddTriangle(
			Vector3(-50.0f, 0.0f, -50.0f),
			Vector3( 50.0f, 0.0f, -50.0f),
			Vector3(-50.0f, 0.0f,  50.0f));
		floor.AddTriangle(
			Vector3( 50.0f, 0.0f, -50.0f),
			Vector3( 50.0f, 0.0f,  50.0f),
			Vector3(-50.0f, 0.0f,  50.0f));
		scene.SetCollidables({ &floor });

		// Create unit standing at origin (feet at Y=0)
		auto unit = std::make_shared<TestGameUnit>(scene, net, project, 0);

		// Set run speed so UnitMovement can compute acceleration (default is 0)
		const float runSpeed = 7.0f; // typical MMO walk speed in m/s
		unit->SetSpeed(movement_type::Run, runSpeed);
		unit->SetSpeed(movement_type::Walk, runSpeed);
		unit->SetSpeed(movement_type::Backwards, runSpeed * 0.5f);

		// Capsule: radius=0.35, halfHeight=0.9 => total height ~2.5m
		// pointA = bottom sphere center, pointB = top sphere center
		const float radius = 0.35f;
		const float halfHeight = 0.9f;
		const Vector3 unitPos(0.0f, 0.0f, 0.0f);
		Capsule capsule(
			unitPos + Vector3(0.0f, radius, 0.0f),
			unitPos + Vector3(0.0f, radius + halfHeight * 2.0f, 0.0f),
			radius);
		unit->SetColliderForTest(capsule);

		// Set unit scene node position to feet (Y=0)
		SceneNode* node = unit->GetSceneNode();
		if (node)
		{
			node->SetPosition(unitPos);
		}

		// Act: walk forward (positive Z) for 2 seconds at 60 Hz (120 ticks)
		SimulateMovement(*unit, Vector3(0.0f, 0.0f, 1.0f), 120, 1.0f / 60.0f);

		// Assert
		const Vector3 pos = unit->GetPosition();
		REQUIRE(!std::isnan(pos.x));
		REQUIRE(!std::isnan(pos.y));
		REQUIRE(!std::isnan(pos.z));
		REQUIRE(pos.z > 1.0f);         // moved forward at least 1 m in 2 seconds
		REQUIRE(pos.y == Approx(0.0f).margin(0.1f)); // stayed on floor (feet at Y~0)
	}
}
