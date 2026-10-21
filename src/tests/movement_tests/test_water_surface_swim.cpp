// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "test_helpers/test_scene.h"
#include "test_helpers/test_net_client.h"
#include "test_helpers/test_game_unit.h"
#include "test_helpers/triangle_collidable.h"
#include "client_data/project.h"
#include "game/movement_ability.h"
#include "math/capsule.h"
#include "math/vector3.h"

// ---------------------------------------------------------------------------
// The server paths creatures on a navigation mesh baked on the ground and lifts the paths of
// creatures which can swim into the water surface band. The observing client has to keep them
// there: snapping them onto the ground below the water, as it does on dry land, made NPCs walk
// along the bottom of lakes fully submerged instead of swimming.
// ---------------------------------------------------------------------------

namespace mmo
{
	namespace
	{
		constexpr float kCapsuleRadius = 0.35f;
		constexpr float kCapsuleLineHalfHeight = 0.65f;

		/// @brief Adds flat ground at y = 0, facing upwards so a downward ray accepts it.
		void AddFlatGround(TriangleCollidable& target)
		{
			target.AddTriangle(
				Vector3(-5.0f, 0.0f, -5.0f),
				Vector3(-5.0f, 0.0f, 5.0f),
				Vector3(5.0f, 0.0f, -5.0f));
			target.AddTriangle(
				Vector3(5.0f, 0.0f, -5.0f),
				Vector3(-5.0f, 0.0f, 5.0f),
				Vector3(5.0f, 0.0f, 5.0f));
		}

		/// @brief Creates a remotely simulated unit (an NPC) at the given position.
		std::shared_ptr<TestGameUnit> CreateRemoteUnit(TestScene& scene, TestNetClient& net,
			const proto_client::Project& project, const Vector3& feetPosition)
		{
			auto unit = std::make_shared<TestGameUnit>(scene, net, project, 0);
			unit->SetLocallyControlledForTest(false);
			unit->SetColliderForTest(Capsule(
				feetPosition + Vector3(0.0f, kCapsuleRadius, 0.0f),
				feetPosition + Vector3(0.0f, kCapsuleRadius + kCapsuleLineHalfHeight * 2.0f, 0.0f),
				kCapsuleRadius));
			unit->GetSceneNode()->SetPosition(feetPosition);
			return unit;
		}
	}

	TEST_CASE("A remote unit in deep water floats at the surface instead of standing on the bottom",
		"[water_surface_swim]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		TriangleCollidable ground;
		AddFlatGround(ground);
		scene.SetCollidables({ &ground });

		// Five units of water above the ground the unit was snapped onto.
		constexpr float waterSurfaceY = 5.0f;
		net.SetWaterSurface(waterSurfaceY);

		const auto remote = CreateRemoteUnit(scene, net, project, Vector3(0.0f, 0.0f, 0.0f));

		CHECK(remote->GetUnitMovement()->CorrectGroundHeight(5.0f, 0.0f));

		CHECK(remote->GetSceneNode()->GetPosition().y == Approx(waterSurfaceY - swim::SurfaceMargin));
		CHECK(remote->GetMovementInfo().IsSwimming());
	}

	TEST_CASE("A remote unit wades through water too shallow to swim in", "[water_surface_swim]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		TriangleCollidable ground;
		AddFlatGround(ground);
		scene.SetCollidables({ &ground });

		// Knee-deep water: the unit keeps standing on the ground below it.
		net.SetWaterSurface(0.5f);

		const auto remote = CreateRemoteUnit(scene, net, project, Vector3(0.0f, 0.0f, 0.0f));

		CHECK(remote->GetUnitMovement()->CorrectGroundHeight(5.0f, 0.0f));

		CHECK(remote->GetSceneNode()->GetPosition().y == Approx(0.0f).margin(0.05f));
		CHECK_FALSE(remote->GetMovementInfo().IsSwimming());
	}

	TEST_CASE("A remote unit above the water surface is not pulled into the water", "[water_surface_swim]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		TriangleCollidable ground;
		AddFlatGround(ground);
		scene.SetCollidables({ &ground });

		// A deep water body whose surface lies below the unit: it stands on a bridge over it.
		net.SetWaterSurface(-10.0f);

		const auto remote = CreateRemoteUnit(scene, net, project, Vector3(0.0f, 0.0f, 0.0f));

		CHECK(remote->GetUnitMovement()->CorrectGroundHeight(5.0f, 0.0f));

		CHECK(remote->GetSceneNode()->GetPosition().y == Approx(0.0f).margin(0.05f));
		CHECK_FALSE(remote->GetMovementInfo().IsSwimming());
	}

	TEST_CASE("A remote unit leaving the water stops swimming", "[water_surface_swim]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		TriangleCollidable ground;
		AddFlatGround(ground);
		scene.SetCollidables({ &ground });

		net.SetWaterSurface(5.0f);

		const auto remote = CreateRemoteUnit(scene, net, project, Vector3(0.0f, 0.0f, 0.0f));
		CHECK(remote->GetUnitMovement()->CorrectGroundHeight(5.0f, 0.0f));
		REQUIRE(remote->GetMovementInfo().IsSwimming());

		// Out of the water body entirely.
		net.SetWaterSurface(std::nullopt);
		CHECK(remote->GetUnitMovement()->CorrectGroundHeight(5.0f, 0.0f));

		CHECK_FALSE(remote->GetMovementInfo().IsSwimming());
		CHECK(remote->GetSceneNode()->GetPosition().y == Approx(0.0f).margin(0.05f));
	}
}
