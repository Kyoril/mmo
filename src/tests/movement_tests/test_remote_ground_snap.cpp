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

// ---------------------------------------------------------------------------
// A remote player is drawn where their own client stands them. The owning client
// rests the character on the floor with its collision capsule, which leaves the
// feet a floor distance above the surface and follows the ground under the whole
// capsule. Resolving the height of the remote copy with a single downward ray at
// the unit's centre instead always lands lower than that, so the character is
// drawn sunk into the floor - a few centimetres on flat ground, more on slopes.
// ---------------------------------------------------------------------------

namespace mmo
{
	namespace
	{
		constexpr float kCapsuleRadius = 0.35f;
		constexpr float kCapsuleLineHalfHeight = 0.65f;  // from GameUnitC::UpdateCollider
		constexpr float kRunSpeed = 7.0f;

		/// @brief Adds a quad rising along +X at the given angle. The winding faces upwards,
		///        which is what a downward ray cast needs to accept the surface as walkable.
		void AddGroundQuad(TriangleCollidable& target, const float angleDegrees)
		{
			const float slope = std::tan(angleDegrees * 3.14159265f / 180.0f);
			const auto heightAt = [slope](const float x) { return x * slope; };

			target.AddTriangle(
				Vector3(-5.0f, heightAt(-5.0f), -5.0f),
				Vector3(-5.0f, heightAt(-5.0f), 5.0f),
				Vector3(5.0f, heightAt(5.0f), -5.0f));
			target.AddTriangle(
				Vector3(5.0f, heightAt(5.0f), -5.0f),
				Vector3(-5.0f, heightAt(-5.0f), 5.0f),
				Vector3(5.0f, heightAt(5.0f), 5.0f));
		}

		std::shared_ptr<TestGameUnit> CreateUnit(TestScene& scene, TestNetClient& net,
			const proto_client::Project& project, const Vector3& feetPosition)
		{
			auto unit = std::make_shared<TestGameUnit>(scene, net, project, 0);
			unit->SetSpeed(movement_type::Run, kRunSpeed);
			unit->SetSpeed(movement_type::Walk, kRunSpeed);
			unit->SetSpeed(movement_type::Backwards, kRunSpeed * 0.5f);

			unit->SetColliderForTest(Capsule(
				feetPosition + Vector3(0.0f, kCapsuleRadius, 0.0f),
				feetPosition + Vector3(0.0f, kCapsuleRadius + kCapsuleLineHalfHeight * 2.0f, 0.0f),
				kCapsuleRadius));
			unit->GetSceneNode()->SetPosition(feetPosition);

			return unit;
		}

		/// @brief Height the owning client's movement simulation rests a standing unit at.
		float SimulateOwnerRestHeight(TestScene& scene, TestNetClient& net,
			const proto_client::Project& project, const Vector3& dropPosition)
		{
			const auto owner = CreateUnit(scene, net, project, dropPosition);
			SimulateMovement(*owner, Vector3::Zero, 120, 1.0f / 60.0f);
			return owner->GetSceneNode()->GetPosition().y;
		}

		/// @brief Runs the per-frame remote ground correction a few times, as the client does.
		float RunRemoteCorrection(TestGameUnit& remote, const float authoritativeY, const int frames = 3)
		{
			for (int i = 0; i < frames; ++i)
			{
				remote.GetUnitMovement()->CorrectRemoteGroundHeight(5.0f, authoritativeY);
			}

			return remote.GetSceneNode()->GetPosition().y;
		}
	}

	TEST_CASE("remote ground correction keeps a unit at its owner's height on flat ground", "[remote_ground_snap]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		TriangleCollidable ground;
		AddGroundQuad(ground, 0.0f);
		scene.SetCollidables({ &ground });

		const float ownerY = SimulateOwnerRestHeight(scene, net, project, Vector3(0.0f, 0.5f, 0.0f));

		// The observing client receives exactly that height and corrects the ground under it.
		const auto remote = CreateUnit(scene, net, project, Vector3(0.0f, ownerY, 0.0f));
		const float remoteY = RunRemoteCorrection(*remote, ownerY);

		INFO("owner Y = " << ownerY << ", remote Y = " << remoteY);
		CHECK(remoteY == Approx(ownerY).margin(0.01f));
	}

	TEST_CASE("remote ground correction keeps a unit at its owner's height on a slope", "[remote_ground_snap]")
	{
		// The steeper the ground under the capsule, the further the centre ray falls short.
		for (const float angle : { 15.0f, 25.0f, 35.0f })
		{
			proto_client::Project project;
			TestScene scene;
			TestNetClient net;

			TriangleCollidable ground;
			AddGroundQuad(ground, angle);
			scene.SetCollidables({ &ground });

			const float ownerY = SimulateOwnerRestHeight(scene, net, project, Vector3(0.0f, 0.5f, 0.0f));

			const auto remote = CreateUnit(scene, net, project, Vector3(0.0f, ownerY, 0.0f));
			const float remoteY = RunRemoteCorrection(*remote, ownerY);

			INFO("slope " << angle << " degrees: owner Y = " << ownerY << ", remote Y = " << remoteY);
			CHECK(remoteY == Approx(ownerY).margin(0.01f));
		}
	}

	TEST_CASE("remote ground correction still recovers a unit far below the floor", "[remote_ground_snap]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		TriangleCollidable ground;
		AddGroundQuad(ground, 0.0f);
		scene.SetCollidables({ &ground });

		const float ownerY = SimulateOwnerRestHeight(scene, net, project, Vector3(0.0f, 0.5f, 0.0f));

		// Dead reckoning dropped the copy well below the floor: no floor is within the capsule's
		// reach any more, so the correction has to fall back to the authoritative ray search.
		const auto remote = CreateUnit(scene, net, project, Vector3(0.0f, -2.0f, 0.0f));
		const float remoteY = RunRemoteCorrection(*remote, ownerY);

		INFO("owner Y = " << ownerY << ", recovered Y = " << remoteY);
		CHECK(remoteY == Approx(ownerY).margin(0.01f));
	}

	TEST_CASE("an idle remote player keeps the owner's landing height while geometry streams in", "[remote_ground_snap][remote_spawn]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;
		TriangleCollidable ground;
		AddGroundQuad(ground, 0.0f);
		scene.SetCollidables({ &ground });

		// The observing client currently has only a lower surface loaded. The owner
		// has already landed on the complete geometry and reported their resting height.
		constexpr float ownerY = 0.32f;
		const auto remote = CreateUnit(scene, net, project, Vector3(0.0f, ownerY, 0.0f));
		scene.GetRootSceneNode().AddChild(*remote->GetSceneNode());
		MovementInfo landed;
		landed.position = Vector3(0.0f, ownerY, 0.0f);
		landed.movementFlags = movement_flags::None;
		remote->EnqueueRemoteMovement(landed);
		for (int frame = 0; frame < 120; ++frame)
		{
			remote->UpdateRemoteMovement(1.0f / 60.0f);
		}
		CHECK(remote->GetPosition().y == Approx(ownerY).margin(0.01f));
	}
}
