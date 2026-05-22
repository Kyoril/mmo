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
	static void AddFlatFloor(TriangleCollidable& floor)
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

	TEST_CASE("flat wall stuck: unit stops at wall with near-zero velocity", "[movement][wall]")
	{
		// Arrange
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		// Large flat floor so unit is on ground, not falling
		TriangleCollidable floor;
		AddFlatFloor(floor);

		// Flat vertical wall at Z=5, normal pointing -Z (toward player starting at Z=0)
		// CCW winding viewed from -Z side (player side) so normal = (0,0,-1)
		TriangleCollidable wall;
		wall.AddTriangle(
			Vector3(-10.0f,  0.0f, 5.0f),
			Vector3(-10.0f, 10.0f, 5.0f),
			Vector3( 10.0f,  0.0f, 5.0f));
		wall.AddTriangle(
			Vector3( 10.0f,  0.0f, 5.0f),
			Vector3(-10.0f, 10.0f, 5.0f),
			Vector3( 10.0f, 10.0f, 5.0f));

		scene.SetCollidables({ &floor, &wall });

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

		// Act: walk toward wall (+Z) for 2 seconds at 60 Hz
		SimulateMovement(*unit, Vector3(0.0f, 0.0f, 1.0f), 120, 1.0f / 60.0f);

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
		INFO("speed = " << speed);

		// NaN guard
		CHECK(!std::isnan(pos.x));
		CHECK(!std::isnan(pos.y));
		CHECK(!std::isnan(pos.z));

		// Unit must not pass through wall
		CHECK(pos.z <= 5.5f);

		// After hitting a flat wall the unit should decelerate to near-zero.
		// Bug 1 (SlideAlongSurface): ProjectToGravityFloor of a flat-wall normal
		// yields a zero vector, which when normalized causes movement to halt —
		// but the unit may also freeze mid-air (velocity not settled to 0).
		// This CHECK is expected to FAIL with the bug present.
		CHECK(speed < 0.5f); // near-zero velocity after settling at wall
	}

	TEST_CASE("near-walkable wall velocity explosion: speed stays bounded", "[movement][wall]")
	{
		// Arrange
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		// Large flat floor so unit is on ground, not falling
		TriangleCollidable floor;
		AddFlatFloor(floor);

		// Near-walkable ramp: normal Y ≈ 0.755 (just above m_walkableFloorY = 0.71)
		// to trigger the TwoWallAdjust division branch.
		// Ramp tilted ~41° from horizontal: cos(41°) ≈ 0.755, sin(41°) ≈ 0.656
		// Vertices computed so CCW winding (from player side) gives normal (0, 0.755, -0.656).
		TriangleCollidable ramp;
		ramp.AddTriangle(
			Vector3(-10.0f, 0.0f,   5.0f),
			Vector3(-10.0f, 8.68f, 15.0f),
			Vector3( 10.0f, 0.0f,   5.0f));
		ramp.AddTriangle(
			Vector3( 10.0f, 0.0f,   5.0f),
			Vector3(-10.0f, 8.68f, 15.0f),
			Vector3( 10.0f, 8.68f, 15.0f));

		scene.SetCollidables({ &floor, &ramp });

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

		// Act: walk toward ramp (+Z) for 2 seconds at 60 Hz
		SimulateMovement(*unit, Vector3(0.0f, 0.0f, 1.0f), 120, 1.0f / 60.0f);

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
		INFO("speed = " << speed << " (runSpeed=" << runSpeed << ", limit=" << 2.0f * runSpeed << ")");

		// NaN guard
		CHECK(!std::isnan(pos.x));
		CHECK(!std::isnan(pos.y));
		CHECK(!std::isnan(pos.z));

		// Bug 2 (TwoWallAdjust): the branch hitNormalY >= m_walkableFloorY amplifies
		// the vertical delta via GetGravitySpaceY(scaledDelta) / hitNormalY, producing
		// a velocity explosion well above 2× runSpeed.
		// This CHECK is expected to FAIL with the bug present.
		CHECK(speed < 2.0f * runSpeed); // speed should stay below 14 m/s
	}
}
