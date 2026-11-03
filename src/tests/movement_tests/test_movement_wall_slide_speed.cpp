// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "test_helpers/test_scene.h"
#include "test_helpers/test_net_client.h"
#include "test_helpers/test_game_unit.h"
#include "test_helpers/triangle_collidable.h"
#include "client_data/project.h"
#include "game/movement_type.h"
#include "math/capsule.h"
#include "math/vector3.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

// ---------------------------------------------------------------------------
// Running into a vertical wall at an angle slides the unit along it. The slide
// must never be faster than running, at any frame rate, or the server's speed
// check flags the player (and the character visibly shoots sideways along the
// wall). A step-up attempt against the wall used to move a fixed, boosted
// distance every tick, which at 144 fps slid the unit at 10+ m/s.
// ---------------------------------------------------------------------------

namespace mmo
{
	namespace
	{
		void addQuad(TriangleCollidable& target, const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d)
		{
			target.AddTriangle(a, b, c);
			target.AddTriangle(a, c, d);
		}

		/// @brief A flat floor at y = 0 and a vertical wall at x = 2 facing -X.
		void addFloorAndWall(TriangleCollidable& floor, TriangleCollidable& wall)
		{
			addQuad(floor,
				Vector3(-30.0f, 0.0f, -30.0f),
				Vector3(-30.0f, 0.0f, 30.0f),
				Vector3(30.0f, 0.0f, 30.0f),
				Vector3(30.0f, 0.0f, -30.0f));
			addQuad(wall,
				Vector3(2.0f, 0.0f, -30.0f),
				Vector3(2.0f, 4.0f, -30.0f),
				Vector3(2.0f, 4.0f, 30.0f),
				Vector3(2.0f, 0.0f, 30.0f));
		}
	}

	TEST_CASE("sliding along a vertical wall is never faster than running", "[movement][wall]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		const bool singleMesh = GENERATE(false, true);
		TriangleCollidable floor;
		TriangleCollidable wall;
		if (singleMesh)
		{
			addFloorAndWall(floor, floor);
			scene.SetCollidables({ &floor });
		}
		else
		{
			addFloorAndWall(floor, wall);
			scene.SetCollidables({ &floor, &wall });
		}

		// Angle between the run direction and the wall normal: 0 runs straight into it.
		const float angleDegrees = GENERATE(0.0f, 5.0f, 20.0f, 45.0f, 70.0f, 85.0f);
		const float frameRate = GENERATE(60.0f, 144.0f, 240.0f);
		const float angle = angleDegrees * 3.1415926535f / 180.0f;
		const Vector3 input(std::cos(angle), 0.0f, std::sin(angle));

		constexpr float runSpeed = 7.0f;
		auto unit = std::make_shared<TestGameUnit>(scene, net, project, 0);
		unit->SetSpeed(movement_type::Run, runSpeed);
		unit->SetSpeed(movement_type::Walk, runSpeed);
		unit->SetSpeed(movement_type::Backwards, runSpeed * 0.5f);
		constexpr float radius = 0.25f;
		unit->SetColliderForTest(Capsule(Vector3(0.0f, radius, 0.0f), Vector3(0.0f, radius + 1.3f, 0.0f), radius));
		unit->GetSceneNode()->SetPosition(Vector3(0.0f, 0.02f, 0.0f));

		UnitMovement& movement = *unit->GetUnitMovement();
		for (int i = 0; i < 30; ++i)
		{
			movement.Tick(1.0f / 60.0f);
		}

		// Run into the wall for 3 s. The server checks the distance between packets (a heartbeat
		// every 0.5 s), so the speed that matters is the one held over half a second.
		const float dt = 1.0f / frameRate;
		const int ticks = static_cast<int>(3.0f * frameRate);
		const int window = static_cast<int>(0.5f * frameRate);
		std::vector<Vector3> positions;
		positions.reserve(ticks);
		for (int i = 0; i < ticks; ++i)
		{
			unit->AddInputVector(input);
			movement.Tick(dt);
			positions.push_back(unit->GetPosition());
		}

		float maxSpeed = 0.0f;
		for (int i = window; i < ticks; ++i)
		{
			const Vector3 travelled = positions[i] - positions[i - window];
			maxSpeed = std::max(maxSpeed, Vector3(travelled.x, 0.0f, travelled.z).GetLength() / (window * dt));
		}

		// Speed along the wall during the last second, when it has settled.
		const float slideSpeed = std::abs(positions[ticks - 1].z - positions[ticks - 1 - static_cast<int>(frameRate)].z);

		const Vector3 pos = unit->GetPosition();
		INFO("singleMesh = " << singleMesh << ", angle = " << angleDegrees << ", frame rate = " << frameRate);
		INFO("max speed over 0.5 s = " << maxSpeed << ", settled slide speed = " << slideSpeed);
		INFO("final = (" << pos.x << ", " << pos.y << ", " << pos.z << ")");

		CHECK(maxSpeed <= runSpeed * 1.01f);
		CHECK(pos.x <= 2.0f - radius + 0.01f);

		// Pushing (almost) straight into the wall must not turn into a run along it.
		if (angleDegrees <= 5.0f)
		{
			CHECK(slideSpeed < 1.5f);
		}
	}
}
