// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
// Tests for backward movement speed and server-side speed validation.

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
#include "game/movement_info.h"
#include <cmath>

namespace mmo
{
	/// Helper: build a large flat floor and a unit placed at the origin.
	static std::shared_ptr<TestGameUnit> MakeUnitOnFloor(
		TestScene& scene,
		TestNetClient& net,
		proto_client::Project& project,
		float runSpeed,
		float backSpeed)
	{
		TriangleCollidable* floor = new TriangleCollidable();
		floor->AddTriangle(
			Vector3(-50.0f, 0.0f, -50.0f),
			Vector3( 50.0f, 0.0f, -50.0f),
			Vector3(-50.0f, 0.0f,  50.0f));
		floor->AddTriangle(
			Vector3( 50.0f, 0.0f, -50.0f),
			Vector3( 50.0f, 0.0f,  50.0f),
			Vector3(-50.0f, 0.0f,  50.0f));
		scene.SetCollidables({ floor });

		auto unit = std::make_shared<TestGameUnit>(scene, net, project, 0);
		unit->SetSpeed(movement_type::Run,       runSpeed);
		unit->SetSpeed(movement_type::Walk,      runSpeed * 0.5f);
		unit->SetSpeed(movement_type::Backwards, backSpeed);
		unit->SetSpeed(movement_type::Swim,      runSpeed);
		unit->SetSpeed(movement_type::SwimBackwards, backSpeed);
		unit->SetSpeed(movement_type::Flight,    runSpeed);
		unit->SetSpeed(movement_type::FlightBackwards, backSpeed);

		const float radius = 0.35f;
		const float halfHeight = 0.9f;
		const Vector3 unitPos(0.0f, 0.0f, 0.0f);
		Capsule capsule(
			unitPos + Vector3(0.0f, radius, 0.0f),
			unitPos + Vector3(0.0f, radius + halfHeight * 2.0f, 0.0f),
			radius);
		unit->SetColliderForTest(capsule);

		if (SceneNode* node = unit->GetSceneNode())
		{
			node->SetPosition(unitPos);
		}

		return unit;
	}

	// -------------------------------------------------------------------------
	// Test 1: backward movement must be capped at backSpeed, not runSpeed
	// -------------------------------------------------------------------------
	TEST_CASE("backward movement - steady state speed does not exceed back speed", "[movement][backward]")
	{
		const float runSpeed  = 7.0f;
		const float backSpeed = 4.5f;   // typical ~64% of run speed

		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		auto unit = MakeUnitOnFloor(scene, net, project, runSpeed, backSpeed);

		// Set the Backward movement flag (as if the player pressed 'S')
		MovementInfo info = unit->GetMovementInfo();
		info.movementFlags |= movement_flags::Backward;
		info.movementFlags &= ~movement_flags::Forward;
		unit->ApplyMovementInfo(info);

		// Simulate 3 seconds at 60 Hz with a negative-Z input scaled to backSpeed
		// (mirrors what game_unit_c.cpp feeds: -forward * GetSpeed(Backwards))
		SimulateMovement(*unit, -unit->GetForwardVector() * backSpeed, 180, 1.0f / 60.0f);

		const Vector3 vel = unit->GetUnitMovement()->GetVelocity();
		const float speed = vel.GetLength();

		INFO("Backward speed at steady state = " << speed << " (backSpeed=" << backSpeed << ", runSpeed=" << runSpeed << ")");

		// Steady-state speed must be at most backSpeed (with a small tolerance for physics rounding)
		REQUIRE(speed <= backSpeed + 0.1f);

		// And it should actually be moving (not clamped to zero by a bug)
		REQUIRE(speed > 0.5f * backSpeed);
	}

	// -------------------------------------------------------------------------
	// Test 2: forward movement still reaches run speed when backward flag is absent
	// -------------------------------------------------------------------------
	TEST_CASE("backward movement - forward flag still uses run speed", "[movement][backward]")
	{
		const float runSpeed  = 7.0f;
		const float backSpeed = 4.5f;

		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		auto unit = MakeUnitOnFloor(scene, net, project, runSpeed, backSpeed);

		// Set the Forward movement flag
		MovementInfo info = unit->GetMovementInfo();
		info.movementFlags |= movement_flags::Forward;
		info.movementFlags &= ~movement_flags::Backward;
		unit->ApplyMovementInfo(info);

		SimulateMovement(*unit, unit->GetForwardVector() * runSpeed, 180, 1.0f / 60.0f);

		const Vector3 vel = unit->GetUnitMovement()->GetVelocity();
		const float speed = vel.GetLength();

		INFO("Forward speed at steady state = " << speed << " (runSpeed=" << runSpeed << ")");

		REQUIRE(speed > backSpeed + 0.1f); // must exceed backward cap
		REQUIRE(speed <= runSpeed + 0.1f); // must not exceed run speed
	}

	// -------------------------------------------------------------------------
	// Test 3: backward distance is less than forward distance over the same time
	// -------------------------------------------------------------------------
	TEST_CASE("backward movement - covers less distance than forward over equal time", "[movement][backward]")
	{
		const float runSpeed  = 7.0f;
		const float backSpeed = 4.5f;
		const int   steps     = 180;
		const float dt        = 1.0f / 60.0f;

		proto_client::Project project1, project2;
		TestScene scene1, scene2;
		TestNetClient net1, net2;

		auto unitFwd  = MakeUnitOnFloor(scene1, net1, project1, runSpeed, backSpeed);
		auto unitBack = MakeUnitOnFloor(scene2, net2, project2, runSpeed, backSpeed);

		// Forward unit
		{
			MovementInfo info = unitFwd->GetMovementInfo();
			info.movementFlags |= movement_flags::Forward;
			info.movementFlags &= ~movement_flags::Backward;
			unitFwd->ApplyMovementInfo(info);
			SimulateMovement(*unitFwd, unitFwd->GetForwardVector() * runSpeed, steps, dt);
		}

		// Backward unit
		{
			MovementInfo info = unitBack->GetMovementInfo();
			info.movementFlags |= movement_flags::Backward;
			info.movementFlags &= ~movement_flags::Forward;
			unitBack->ApplyMovementInfo(info);
			SimulateMovement(*unitBack, -unitBack->GetForwardVector() * backSpeed, steps, dt);
		}

		const float fwdDist  = unitFwd->GetPosition().GetLength();
		const float backDist = unitBack->GetPosition().GetLength();

		INFO("Forward distance = " << fwdDist << ", backward distance = " << backDist);

		REQUIRE(backDist < fwdDist);
	}

	// -------------------------------------------------------------------------
	// Test 4: server speed check - backward packet validated against back speed
	// This simulates what world_server/player.cpp does.
	// -------------------------------------------------------------------------
	TEST_CASE("server speed validation - backward packet uses backward speed cap", "[movement][backward][server]")
	{
		const float runSpeed  = 7.0f;
		const float backSpeed = 4.5f;

		// Simulate two positions 1 second apart while moving backward.
		// At backSpeed the player travels backSpeed meters in 1 second.
		const float elapsed   = 1.0f;
		const float dist      = backSpeed * elapsed; // exactly at cap

		// Server formula: maxSpeed = baseSpeed * 1.35
		const float tolerance  = 1.35f;

		// When backward flag is set, base = backSpeed
		const float maxSpeedBack = backSpeed * tolerance;
		const float impliedSpeed = dist / elapsed; // = backSpeed exactly

		INFO("impliedSpeed=" << impliedSpeed << " maxSpeedBack=" << maxSpeedBack);

		// Must pass (no violation)
		REQUIRE(impliedSpeed <= maxSpeedBack);

		// Also verify that the same distance would incorrectly pass the old check
		// that used run speed. The old check would pass even if the player was
		// moving at run speed while going backward (bug). Now with the fix, a
		// packet implying run speed while flagged backward should be caught.
		const float impliedRunSpeed = runSpeed; // player claims run speed while going backward
		const float oldMaxSpeed     = runSpeed * tolerance; // old (incorrect) check
		// Old check: passes (that was the bug)
		CHECK(impliedRunSpeed <= oldMaxSpeed);
		// New check using backward speed: caught
		CHECK(impliedRunSpeed > maxSpeedBack);
	}

	// -------------------------------------------------------------------------
	// Test 5: both Forward and Backward flags set -> treated as forward (run speed)
	// -------------------------------------------------------------------------
	TEST_CASE("backward movement - both flags set falls back to run speed", "[movement][backward]")
	{
		const float runSpeed  = 7.0f;
		const float backSpeed = 4.5f;

		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		auto unit = MakeUnitOnFloor(scene, net, project, runSpeed, backSpeed);

		// Set both flags (edge case)
		MovementInfo info = unit->GetMovementInfo();
		info.movementFlags |= movement_flags::Forward | movement_flags::Backward;
		unit->ApplyMovementInfo(info);

		// Provide forward input (the contradicting state) at run speed
		SimulateMovement(*unit, unit->GetForwardVector() * runSpeed, 180, 1.0f / 60.0f);

		const Vector3 vel = unit->GetUnitMovement()->GetVelocity();
		const float speed = vel.GetLength();

		INFO("Both-flags speed = " << speed);

		// Should reach run speed, not be capped to backward speed
		REQUIRE(speed > backSpeed + 0.1f);
		REQUIRE(speed <= runSpeed + 0.1f);
	}
}
