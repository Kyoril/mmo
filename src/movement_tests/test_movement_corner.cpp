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
	// Helper: build a large flat floor covering [-50,50] in XZ at Y=0
	static void AddFlatFloorCorner(TriangleCollidable& floor)
	{
		floor.AddTriangle(
			Vector3(-50.0f, 0.0f, -50.0f),
			Vector3( 50.0f, 0.0f, -50.0f),
			Vector3(-50.0f, 0.0f,  50.0f));
		floor.AddTriangle(
			Vector3( 50.0f, 0.0f, -50.0f),
			Vector3( 50.0f, 0.0f,  50.0f),
			Vector3(-50.0f, 0.0f,  50.0f));
	}

	TEST_CASE("two-wall corner: unit stops cleanly without NaN", "[movement][corner]")
	{
		// Arrange
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		// Floor at Y=0
		TriangleCollidable floor;
		AddFlatFloorCorner(floor);

		// Wall 1 at Z=5, normal facing -Z (toward player at origin)
		// CCW winding viewed from -Z (player side): normal = (0,0,-1)
		TriangleCollidable wall1;
		wall1.AddTriangle(
			Vector3(-10.0f,  0.0f, 5.0f),
			Vector3(-10.0f, 10.0f, 5.0f),
			Vector3( 10.0f,  0.0f, 5.0f));
		wall1.AddTriangle(
			Vector3( 10.0f,  0.0f, 5.0f),
			Vector3(-10.0f, 10.0f, 5.0f),
			Vector3( 10.0f, 10.0f, 5.0f));

		// Wall 2 at X=5, normal facing -X (toward player at origin)
		// CCW winding viewed from -X (player side): normal = (-1,0,0)
		TriangleCollidable wall2;
		wall2.AddTriangle(
			Vector3(5.0f,  0.0f, -10.0f),
			Vector3(5.0f, 10.0f, -10.0f),
			Vector3(5.0f,  0.0f,  10.0f));
		wall2.AddTriangle(
			Vector3(5.0f,  0.0f,  10.0f),
			Vector3(5.0f, 10.0f, -10.0f),
			Vector3(5.0f, 10.0f,  10.0f));

		scene.SetCollidables({ &floor, &wall1, &wall2 });

		auto unit = std::make_shared<TestGameUnit>(scene, net, project, 0);

		const float runSpeed = 7.0f;
		unit->SetSpeed(movement_type::Run, runSpeed);
		unit->SetSpeed(movement_type::Walk, runSpeed);
		unit->SetSpeed(movement_type::Backwards, runSpeed * 0.5f);

		const float radius = 0.35f;
		const float halfHeight = 0.9f;
		const Vector3 unitPos(0.0f, 0.0f, 0.0f);
		Capsule capsule(
			unitPos + Vector3(0.0f, radius, 0.0f),
			unitPos + Vector3(0.0f, radius + halfHeight * 2.0f, 0.0f),
			radius);
		unit->SetColliderForTest(capsule);

		SceneNode* node = unit->GetSceneNode();
		if (node)
		{
			node->SetPosition(unitPos);
		}

		// Act — diagonal input toward the corner (X+, Z+) for 2 seconds at 60 fps
		SimulateMovement(*unit, Vector3(0.707f, 0.0f, 0.707f), 120, 1.0f / 60.0f);

		// Capture final state
		const Vector3 pos = unit->GetPosition();
		const Vector3 vel = unit->GetUnitMovement()->GetVelocity();
		const float speed = std::sqrt(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);

		INFO("pos.x = " << pos.x);
		INFO("pos.y = " << pos.y);
		INFO("pos.z = " << pos.z);
		INFO("vel.x = " << vel.x);
		INFO("vel.y = " << vel.y);
		INFO("vel.z = " << vel.z);
		INFO("speed = " << speed << "  runSpeed = " << runSpeed);

		// Assert — no NaN
		CHECK(!std::isnan(pos.x));
		CHECK(!std::isnan(pos.y));
		CHECK(!std::isnan(pos.z));

		// Assert — unit did not pass through walls
		CHECK(pos.x <= 5.5f); // did not pass through wall 2 (X=5)
		CHECK(pos.z <= 5.5f); // did not pass through wall 1 (Z=5)

		// Assert — velocity did not explode in corner
		CHECK(speed < 2.0f * runSpeed);
	}
}
