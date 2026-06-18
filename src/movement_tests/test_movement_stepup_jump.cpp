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
#include <algorithm>

namespace mmo
{
	// ---------------------------------------------------------------------------
	// Shared helpers
	// ---------------------------------------------------------------------------
	namespace
	{
		/// Build a standard capsule at origin with radius=0.35, halfHeight=0.9.
		Capsule MakeStandardCapsule(const Vector3& pos)
		{
			const float radius     = 0.35f;
			const float halfHeight = 0.9f;
			return Capsule(
				pos + Vector3(0.0f, radius, 0.0f),
				pos + Vector3(0.0f, radius + halfHeight * 2.0f, 0.0f),
				radius);
		}

		/// Configure movement speeds on a unit.
		void SetStandardSpeeds(TestGameUnit& unit)
		{
			const float run = 7.0f;
			unit.SetSpeed(movement_type::Run,       run);
			unit.SetSpeed(movement_type::Walk,      run);
			unit.SetSpeed(movement_type::Backwards, run * 0.5f);
		}
	}

	// ---------------------------------------------------------------------------
	// Test: Step-up scenario
	// ---------------------------------------------------------------------------
	TEST_CASE("step-up ledge - unit climbs ledge below maxStepHeight", "[movement][stepup]")
	{
		proto_client::Project project;
		TestScene             scene;
		TestNetClient         net;

		// ---- Geometry -----------------------------------------------------------
		// Large flat floor at Y=0, [-50,50] in XZ
		TriangleCollidable floor;
		floor.AddTriangle(
			Vector3(-50.0f, 0.0f, -50.0f),
			Vector3( 50.0f, 0.0f, -50.0f),
			Vector3(-50.0f, 0.0f,  50.0f));
		floor.AddTriangle(
			Vector3( 50.0f, 0.0f, -50.0f),
			Vector3( 50.0f, 0.0f,  50.0f),
			Vector3(-50.0f, 0.0f,  50.0f));

		// Ledge vertical face at Z=3.0, height 0.3 m (< maxStepHeight 0.45 m).
		// Winding: normal facing -Z (toward player approaching from Z=0).
		TriangleCollidable ledgeFace;
		ledgeFace.AddTriangle(
			Vector3(-5.0f,  0.0f,  3.0f),
			Vector3( 5.0f,  0.3f,  3.0f),
			Vector3( 5.0f,  0.0f,  3.0f));
		ledgeFace.AddTriangle(
			Vector3(-5.0f,  0.0f,  3.0f),
			Vector3(-5.0f,  0.3f,  3.0f),
			Vector3( 5.0f,  0.3f,  3.0f));

		// Ledge top surface at Y=0.3 from Z=3 to Z=20.
		// Winding: normal facing +Y (CCW from above).
		TriangleCollidable ledgeTop;
		ledgeTop.AddTriangle(
			Vector3(-5.0f,  0.3f,  3.0f),
			Vector3(-5.0f,  0.3f, 20.0f),
			Vector3( 5.0f,  0.3f, 20.0f));
		ledgeTop.AddTriangle(
			Vector3(-5.0f,  0.3f,  3.0f),
			Vector3( 5.0f,  0.3f, 20.0f),
			Vector3( 5.0f,  0.3f,  3.0f));

		scene.SetCollidables({ &floor, &ledgeFace, &ledgeTop });

		// ---- Unit setup ---------------------------------------------------------
		const Vector3 startPos(0.0f, 0.0f, 0.0f);
		auto unit = std::make_shared<TestGameUnit>(scene, net, project, 0);
		SetStandardSpeeds(*unit);
		unit->SetColliderForTest(MakeStandardCapsule(startPos));

		SceneNode* node = unit->GetSceneNode();
		if (node) { node->SetPosition(startPos); }

		// ---- Act: walk forward (+Z) for 2 seconds at 60 Hz ----------------------
		SimulateMovement(*unit, Vector3(0.0f, 0.0f, 1.0f), 120, 1.0f / 60.0f);

		// ---- Assert --------------------------------------------------------------
		const Vector3 pos = unit->GetPosition();
		const Vector3 vel = unit->GetUnitMovement() ? unit->GetUnitMovement()->GetVelocity() : Vector3::Zero;

		INFO("pos.x = " << pos.x);
		INFO("pos.y = " << pos.y << "  (expected >= 0.25)");
		INFO("pos.z = " << pos.z);
		INFO("vel.x = " << vel.x);
		INFO("vel.y = " << vel.y);
		INFO("vel.z = " << vel.z);

		REQUIRE(!std::isnan(pos.x));
		REQUIRE(!std::isnan(pos.y));
		REQUIRE(!std::isnan(pos.z));
		// After 2 seconds the player must have crossed the ledge and be at its height.
		REQUIRE(pos.y >= Approx(0.25f));
	}

	// ---------------------------------------------------------------------------
	// Test: Jump + land scenario
	// ---------------------------------------------------------------------------
	TEST_CASE("jump and land - unit leaves floor, peaks above 0.5 m, returns to floor", "[movement][jump]")
	{
		proto_client::Project project;
		TestScene             scene;
		TestNetClient         net;

		// ---- Geometry: flat floor only ------------------------------------------
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

		// ---- Unit setup ---------------------------------------------------------
		const Vector3 startPos(0.0f, 0.0f, 0.0f);
		auto unit = std::make_shared<TestGameUnit>(scene, net, project, 0);
		SetStandardSpeeds(*unit);
		unit->SetColliderForTest(MakeStandardCapsule(startPos));

		SceneNode* node = unit->GetSceneNode();
		if (node) { node->SetPosition(startPos); }

		// ---- Act: trigger jump then simulate 2 seconds --------------------------
		UnitMovement*  movement = unit->GetUnitMovement();
		unit->Jump();  // sets m_pressedJump = true before first tick

		float          peakY    = 0.0f;
		for (int i = 0; i < 120; ++i)
		{
			movement->Tick(1.0f / 60.0f);
			peakY = std::max(peakY, unit->GetPosition().y);
		}

		// ---- Assert --------------------------------------------------------------
		const Vector3 pos = unit->GetPosition();
		const Vector3 vel = movement ? movement->GetVelocity() : Vector3::Zero;

		// Unconditional diagnostics so S04 can diagnose landing issues.
		INFO("peakY      = " << peakY);
		INFO("final pos  = (" << pos.x << ", " << pos.y << ", " << pos.z << ")");
		INFO("final vel  = (" << vel.x << ", " << vel.y << ", " << vel.z << ")");

		REQUIRE(!std::isnan(pos.x));
		REQUIRE(!std::isnan(pos.y));
		REQUIRE(!std::isnan(pos.z));
		REQUIRE(!std::isnan(vel.x));
		REQUIRE(!std::isnan(vel.y));
		REQUIRE(!std::isnan(vel.z));
		REQUIRE(peakY > 0.5f);                               // unit actually left the floor
		REQUIRE(pos.y == Approx(0.0f).margin(0.2f));         // landed back on floor
	}
}
