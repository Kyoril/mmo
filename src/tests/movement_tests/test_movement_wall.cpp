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
#include <algorithm>
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

	// ─────────────────────────────────────────────────────────────────────────
	// Wall-with-roof speed-glide bug
	// ─────────────────────────────────────────────────────────────────────────
	// Geometry: a tall vertical wall (Z=5) with a horizontal overhang/roof cap
	// sitting at Y=0.6 (just above maxStepHeight=0.45), approached at a 45°
	// angle (+X +Z direction).
	//
	// Hypothesis: at 45°, CanStepUp returns true (flat wall hit).  StepUp sweeps
	// upward, hits the roof overhang (blocking), then tries to slide forward
	// inside StepUp.  When StepUp returns true despite the blocked upward sweep,
	// MoveAlongFloor recalculates velocity as (newPos - prePos)/dt using a
	// position that was partially altered by the overhang, producing a large
	// sideways velocity spike (speed glide bug).
	// ─────────────────────────────────────────────────────────────────────────
	TEST_CASE("wall with roof overhang: 45-degree approach must not cause speed glide", "[movement][wall][stepup]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		// Flat floor at Y=0
		TriangleCollidable floor;
		AddFlatFloor(floor);

		// Vertical wall face: X in [-10,10], Y in [0,5], at Z=5.
		// Normal points toward player (-Z), CCW from -Z side.
		TriangleCollidable wall;
		wall.AddTriangle(
			Vector3(-10.0f, 0.0f, 5.0f),
			Vector3(-10.0f, 5.0f, 5.0f),
			Vector3( 10.0f, 0.0f, 5.0f));
		wall.AddTriangle(
			Vector3( 10.0f, 0.0f, 5.0f),
			Vector3(-10.0f, 5.0f, 5.0f),
			Vector3( 10.0f, 5.0f, 5.0f));

		// Horizontal roof/overhang: a thin slab at Y=0.6 extending from Z=4 to Z=6
		// (straddles the wall face).  Normal points down (-Y), CCW from above.
		// This blocks the StepUp upward sweep for any unit capsule taller than 0.6m.
		TriangleCollidable roof;
		roof.AddTriangle(
			Vector3(-10.0f, 0.6f, 4.0f),
			Vector3( 10.0f, 0.6f, 4.0f),
			Vector3(-10.0f, 0.6f, 6.0f));
		roof.AddTriangle(
			Vector3( 10.0f, 0.6f, 4.0f),
			Vector3( 10.0f, 0.6f, 6.0f),
			Vector3(-10.0f, 0.6f, 6.0f));

		scene.SetCollidables({ &floor, &wall, &roof });

		auto unit = std::make_shared<TestGameUnit>(scene, net, project, 0);

		const float runSpeed = 7.0f;
		unit->SetSpeed(movement_type::Run, runSpeed);
		unit->SetSpeed(movement_type::Walk, runSpeed);
		unit->SetSpeed(movement_type::Backwards, runSpeed * 0.5f);

		const float radius = 0.35f;
		const float halfHeight = 0.9f;  // total capsule height = 2*halfHeight = 1.8m > roof at 0.6m
		const Vector3 startPos(0.0f, 0.0f, 0.0f);
		Capsule capsule(
			startPos + Vector3(0.0f, radius, 0.0f),
			startPos + Vector3(0.0f, radius + halfHeight * 2.0f, 0.0f),
			radius);
		unit->SetColliderForTest(capsule);

		SceneNode* node = unit->GetSceneNode();
		if (node) node->SetPosition(startPos);

		// Approach at 45 degrees (normalized +X +Z direction)
		const Vector3 inputDir = Vector3(1.0f, 0.0f, 1.0f).NormalizedCopy();

		// Track peak speed across all ticks — the glide bug is a transient spike
		// (one bad frame is enough to teleport the player in-game).
		float peakSpeed = 0.0f;
		int peakFrame = -1;
		Vector3 peakVel;

		SimulateMovement(*unit, inputDir, 120, 1.0f / 60.0f,
			[&](int step, const Vector3& /*pos*/, const Vector3& v)
			{
				const float s = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
				if (s > peakSpeed)
				{
					peakSpeed = s;
					peakFrame = step;
					peakVel = v;
				}
			});

		const Vector3 pos = unit->GetPosition();
		const Vector3 vel = unit->GetUnitMovement()->GetVelocity();
		const float velXZ = std::sqrt(vel.x * vel.x + vel.z * vel.z);
		const float speed  = std::sqrt(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);

		INFO("pos = (" << pos.x << ", " << pos.y << ", " << pos.z << ")");
		INFO("vel = (" << vel.x << ", " << vel.y << ", " << vel.z << ")");
		INFO("speed = " << speed << "  velXZ = " << velXZ);
		INFO("peakSpeed = " << peakSpeed << " at frame " << peakFrame
			<< "  peakVel=(" << peakVel.x << "," << peakVel.y << "," << peakVel.z << ")");
		INFO("runSpeed = " << runSpeed << "  limit = " << 2.0f * runSpeed);

		// NaN / inf guard
		CHECK(!std::isnan(pos.x));
		CHECK(!std::isnan(pos.y));
		CHECK(!std::isnan(pos.z));
		CHECK(!std::isinf(vel.x));
		CHECK(!std::isinf(vel.y));
		CHECK(!std::isinf(vel.z));

		// Unit must not pass through the wall
		CHECK(pos.z <= 5.5f);

		// Speed glide bug: after 2s against the wall+roof, final speed must stay
		// below 2× runSpeed.  A buggy StepUp will produce a velocity spike that
		// persists well above this threshold.
		CHECK(speed < 2.0f * runSpeed);

		// Additionally, horizontal (XZ) speed must stay reasonable — the glide
		// specifically manifests as a lateral velocity explosion, not vertical.
		CHECK(velXZ < 2.0f * runSpeed);

		// Peak-speed check catches transient one-frame spikes that normalise by
		// the end of the simulation but still cause visible teleportation in-game.
		CHECK(peakSpeed < 2.0f * runSpeed);
	}
}
