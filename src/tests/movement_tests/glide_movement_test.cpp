// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "test_helpers/test_scene.h"
#include "test_helpers/test_net_client.h"
#include "test_helpers/test_game_unit.h"
#include "test_helpers/triangle_collidable.h"
#include "test_helpers/simulate_movement.h"
#include "client_data/project.h"
#include "game/movement_info.h"
#include "game/movement_type.h"
#include "game/traversal.h"
#include "game_client/movement_event.h"
#include "math/capsule.h"
#include "math/vector3.h"

#include <cmath>
#include <memory>

// ---------------------------------------------------------------------------
// While the Gliding movement flag is set, a falling unit does not accelerate with gravity but
// follows StepGlideVelocity: it sinks at the glide sink speed, rises inside updrafts and moves
// at the glide forward speed with input. The server validates glides against exactly that model,
// so the own unit and the remote copies on other clients must both use it. Landing and entering
// water end a glide without a MoveStopGlide (the server treats them as implicit stops).
// ---------------------------------------------------------------------------

namespace mmo
{
	namespace
	{
		constexpr float kCapsuleRadius = 0.35f;
		constexpr float kCapsuleLineHalfHeight = 0.65f;
		constexpr float kRunSpeed = 7.0f;
		constexpr float kFrame = 1.0f / 60.0f;

		const TraversalSettings kSettings;

		void AddFloor(TriangleCollidable& target)
		{
			target.AddTriangle(
				Vector3(-50.0f, 0.0f, -50.0f),
				Vector3(50.0f, 0.0f, -50.0f),
				Vector3(-50.0f, 0.0f, 50.0f));
			target.AddTriangle(
				Vector3(50.0f, 0.0f, -50.0f),
				Vector3(50.0f, 0.0f, 50.0f),
				Vector3(-50.0f, 0.0f, 50.0f));
		}

		std::shared_ptr<TestGameUnit> CreateUnit(TestScene& scene, TestNetClient& net,
			const proto_client::Project& project, const Vector3& feet, const bool locallyControlled)
		{
			auto unit = std::make_shared<TestGameUnit>(scene, net, project, 0);
			unit->SetLocallyControlledForTest(locallyControlled);
			unit->SetSpeed(movement_type::Run, kRunSpeed);
			unit->SetSpeed(movement_type::Walk, kRunSpeed);
			unit->SetSpeed(movement_type::Backwards, kRunSpeed * 0.5f);
			unit->SetColliderForTest(Capsule(
				feet + Vector3(0.0f, kCapsuleRadius, 0.0f),
				feet + Vector3(0.0f, kCapsuleRadius + kCapsuleLineHalfHeight * 2.0f, 0.0f),
				kCapsuleRadius));
			// Attached like GameObjectC::SetupSceneObjects does in the client (the remote path uses
			// SetDerivedPosition, which is a no-op on a node without a parent).
			scene.GetRootSceneNode().AddChild(*unit->GetSceneNode());
			unit->GetSceneNode()->SetPosition(feet);
			return unit;
		}

		/// Creates the own unit falling at the given speed with the glider open.
		std::shared_ptr<TestGameUnit> CreateGlidingOwnUnit(TestScene& scene, TestNetClient& net,
			const proto_client::Project& project, const Vector3& feet, const float verticalSpeed)
		{
			auto unit = CreateUnit(scene, net, project, feet, true);
			unit->GetUnitMovement()->SetMovementMode(MovementMode::Falling);
			unit->GetUnitMovement()->SetVelocity(Vector3(0.0f, verticalSpeed, 0.0f));
			unit->StartGlide();
			return unit;
		}

		MovementInfo GlidePacket(const Vector3& position, const Vector3& velocity, const GameTime timestamp, const uint32 extraFlags = 0)
		{
			MovementInfo info;
			info.position = position;
			info.facing = Radian(0.0f);  // facing +X
			info.movementFlags = movement_flags::Falling | movement_flags::Gliding | extraFlags;
			info.timestamp = timestamp;
			info.jumpVelocity = velocity;
			return info;
		}
	}

	TEST_CASE("Own unit with Gliding flag sinks at glide speed", "[glide]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		// Opened in a fast fall: the glider brakes it to the sink speed.
		const auto unit = CreateGlidingOwnUnit(scene, net, project, Vector3(0.0f, 100.0f, 0.0f), -15.0f);
		REQUIRE(unit->IsGliding());
		CHECK(net.HasMoveEvent(movement_event_type::StartGlide));

		SimulateMovement(*unit, Vector3::Zero, 120, kFrame);

		const Vector3 velocity = unit->GetUnitMovement()->GetVelocity();
		INFO("velocity = (" << velocity.x << ", " << velocity.y << ", " << velocity.z << ")");
		CHECK(unit->IsGliding());
		CHECK(unit->GetUnitMovement()->IsFalling());
		CHECK(velocity.y == Approx(-kSettings.glideSinkSpeed).margin(0.01f));
	}

	TEST_CASE("Own gliding unit moves at the glide forward speed with input", "[glide]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		const auto unit = CreateGlidingOwnUnit(scene, net, project, Vector3(0.0f, 100.0f, 0.0f), 0.0f);
		const Vector3 start = unit->GetPosition();

		SimulateMovement(*unit, Vector3(kRunSpeed, 0.0f, 0.0f), 60, kFrame);

		const Vector3 velocity = unit->GetUnitMovement()->GetVelocity();
		const float horizontal = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
		INFO("velocity = (" << velocity.x << ", " << velocity.y << ", " << velocity.z << ")");
		CHECK(horizontal == Approx(kSettings.glideForwardSpeed).margin(0.01f));
		CHECK(velocity.x > 0.0f);

		// One second at 10 m/s: the server allows at most 13.5 m.
		const float travelled = (unit->GetPosition() - start).GetLength();
		CHECK(travelled <= kSettings.glideForwardSpeed * 1.35f);
	}

	TEST_CASE("Remote unit with Gliding flag sinks at glide speed", "[glide]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		const Vector3 start(0.0f, 100.0f, 0.0f);
		const auto remote = CreateUnit(scene, net, project, start, false);
		remote->EnqueueRemoteMovement(GlidePacket(start, Vector3(0.0f, -15.0f, 0.0f), 1000));

		for (int frame = 0; frame < 120; ++frame)
		{
			remote->UpdateRemoteMovement(kFrame);
		}

		const Vector3 velocity = remote->GetUnitMovement()->GetVelocity();
		INFO("velocity = (" << velocity.x << ", " << velocity.y << ", " << velocity.z << ")");
		CHECK(remote->IsGliding());
		CHECK(velocity.y == Approx(-kSettings.glideSinkSpeed).margin(0.01f));

		// Braking from 15 m/s takes about a second, then it sinks at 2.5 m/s: far less than the
		// ~40 m a free fall would cover in two seconds.
		const float fallen = start.y - remote->GetPosition().y;
		INFO("fallen = " << fallen);
		CHECK(fallen < 15.0f);
	}

	TEST_CASE("Updraft provider lifts a gliding unit", "[glide]")
	{
		constexpr float updraft = 4.0f;

		SECTION("own unit")
		{
			proto_client::Project project;
			TestScene scene;
			TestNetClient net;

			const auto unit = CreateGlidingOwnUnit(scene, net, project, Vector3(0.0f, 50.0f, 0.0f), -2.5f);
			unit->SetUpdraftProvider([](const Vector3&) { return updraft; });
			const float startY = unit->GetPosition().y;

			SimulateMovement(*unit, Vector3::Zero, 120, kFrame);

			const Vector3 velocity = unit->GetUnitMovement()->GetVelocity();
			INFO("velocity.y = " << velocity.y);
			CHECK(velocity.y == Approx(updraft).margin(0.01f));
			CHECK(unit->GetPosition().y > startY);
		}

		SECTION("remote unit")
		{
			proto_client::Project project;
			TestScene scene;
			TestNetClient net;

			const Vector3 start(0.0f, 50.0f, 0.0f);
			const auto remote = CreateUnit(scene, net, project, start, false);
			remote->SetUpdraftProvider([](const Vector3&) { return updraft; });
			remote->EnqueueRemoteMovement(GlidePacket(start, Vector3(0.0f, -2.5f, 0.0f), 1000));

			for (int frame = 0; frame < 120; ++frame)
			{
				remote->UpdateRemoteMovement(kFrame);
			}

			const Vector3 velocity = remote->GetUnitMovement()->GetVelocity();
			INFO("velocity.y = " << velocity.y);
			CHECK(velocity.y == Approx(updraft).margin(0.01f));
			CHECK(remote->GetPosition().y > start.y);
		}

		SECTION("a provider outside of every updraft does not lift")
		{
			proto_client::Project project;
			TestScene scene;
			TestNetClient net;

			const auto unit = CreateGlidingOwnUnit(scene, net, project, Vector3(0.0f, 50.0f, 0.0f), 0.0f);
			unit->SetUpdraftProvider([](const Vector3&) { return 0.0f; });

			SimulateMovement(*unit, Vector3::Zero, 60, kFrame);

			CHECK(unit->GetUnitMovement()->GetVelocity().y == Approx(-kSettings.glideSinkSpeed).margin(0.01f));
		}
	}

	TEST_CASE("Landing clears Gliding", "[glide]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		TriangleCollidable floor;
		AddFloor(floor);
		scene.SetCollidables({ &floor });

		const auto unit = CreateGlidingOwnUnit(scene, net, project, Vector3(0.0f, 2.0f, 0.0f), 0.0f);
		REQUIRE(unit->IsGliding());

		// 2 m at 2.5 m/s: on the ground well within two seconds.
		SimulateMovement(*unit, Vector3::Zero, 120, kFrame);

		CHECK_FALSE(unit->GetUnitMovement()->IsFalling());
		CHECK_FALSE(unit->IsGliding());
		CHECK_FALSE(unit->GetMovementInfo().IsGliding());
		CHECK(net.HasMoveEvent(movement_event_type::Land));

		// The server ends the glide with the landing itself: no MoveStopGlide.
		CHECK_FALSE(net.HasMoveEvent(movement_event_type::StopGlide));
	}

	TEST_CASE("Entering water clears Gliding without a stop event", "[glide]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		TriangleCollidable floor;
		AddFloor(floor);
		scene.SetCollidables({ &floor });
		net.SetWaterSurface(5.0f);

		const auto unit = CreateGlidingOwnUnit(scene, net, project, Vector3(0.0f, 7.0f, 0.0f), 0.0f);
		// Only player characters simulate swimming.
		unit->SetReportsAsPlayerForTest(true);
		REQUIRE(unit->IsGliding());

		SimulateMovement(*unit, Vector3::Zero, 180, kFrame);

		CHECK(unit->GetMovementInfo().IsSwimming());
		CHECK_FALSE(unit->IsGliding());
		CHECK(net.HasMoveEvent(movement_event_type::StartSwim));

		// The server ends the glide with MoveStartSwim itself: no MoveStopGlide.
		CHECK_FALSE(net.HasMoveEvent(movement_event_type::StopGlide));
	}

	TEST_CASE("Stopping a glide raises a stop event and restores gravity", "[glide]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		const auto unit = CreateGlidingOwnUnit(scene, net, project, Vector3(0.0f, 100.0f, 0.0f), 0.0f);
		SimulateMovement(*unit, Vector3::Zero, 30, kFrame);

		// Pressing jump while gliding closes the glider.
		CHECK(unit->TryToggleGlide());
		CHECK_FALSE(unit->IsGliding());
		CHECK(unit->GetMovementInfo().IsFalling());
		CHECK(net.HasMoveEvent(movement_event_type::StopGlide));

		SimulateMovement(*unit, Vector3::Zero, 60, kFrame);
		CHECK(unit->GetUnitMovement()->GetVelocity().y < -10.0f);
	}

	TEST_CASE("The glider does not open without the Glider spell", "[glide]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		const auto unit = CreateUnit(scene, net, project, Vector3(0.0f, 100.0f, 0.0f), true);
		unit->GetUnitMovement()->SetMovementMode(MovementMode::Falling);

		CHECK_FALSE(unit->TryToggleGlide());
		CHECK_FALSE(unit->IsGliding());
		CHECK_FALSE(net.HasMoveEvent(movement_event_type::StartGlide));
	}
}
