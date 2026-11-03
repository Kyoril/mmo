// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "test_helpers/test_scene.h"
#include "test_helpers/test_net_client.h"
#include "test_helpers/test_game_unit.h"
#include "test_helpers/triangle_collidable.h"
#include "client_data/project.h"
#include "game/movement_info.h"
#include "game/movement_type.h"
#include "math/capsule.h"
#include "math/vector3.h"

#include <algorithm>
#include <cmath>
#include <memory>

// ---------------------------------------------------------------------------
// Between two packets an observing client dead-reckons a remote player from its
// last movement flags. When the real player is stopped by an obstacle (a wall it
// runs into, the top of a box it jumps onto), the prediction keeps going — but the
// drawn character must not follow it into the geometry. It has to stay outside,
// exactly where the owning client's capsule is stopped, instead of being dragged
// into the obstacle and pulled back out by the next heartbeat.
// ---------------------------------------------------------------------------

namespace mmo
{
	namespace
	{
		constexpr float kRadius = 0.25f;  // GameUnitC::UpdateCollider forces this radius
		constexpr float kLineHalfHeight = 0.65f;  // from GameUnitC::UpdateCollider
		constexpr float kRunSpeed = 7.0f;
		constexpr float kFrame = 1.0f / 60.0f;
		constexpr float kFloorGap = 0.022f;

		void addQuad(TriangleCollidable& target, const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d)
		{
			target.AddTriangle(a, b, c);
			target.AddTriangle(a, c, d);
		}

		void addFloor(TriangleCollidable& target)
		{
			addQuad(target,
				Vector3(-20.0f, 0.0f, -20.0f),
				Vector3(-20.0f, 0.0f, 20.0f),
				Vector3(20.0f, 0.0f, 20.0f),
				Vector3(20.0f, 0.0f, -20.0f));
		}

		std::shared_ptr<TestGameUnit> createRemote(TestScene& scene, TestNetClient& net, const proto_client::Project& project, const Vector3& feet)
		{
			auto unit = std::make_shared<TestGameUnit>(scene, net, project, 0);
			unit->SetLocallyControlledForTest(false);
			unit->SetSpeed(movement_type::Run, kRunSpeed);
			unit->SetSpeed(movement_type::Walk, kRunSpeed);
			unit->SetSpeed(movement_type::Backwards, kRunSpeed * 0.5f);
			unit->SetColliderForTest(Capsule(
				feet + Vector3(0.0f, kRadius, 0.0f),
				feet + Vector3(0.0f, kRadius + kLineHalfHeight * 2.0f, 0.0f),
				kRadius));
			// Attached like GameObjectC::SetupSceneObjects does in the client: SetDerivedPosition,
			// which the remote path uses, is a no-op on a node without a parent.
			scene.GetRootSceneNode().AddChild(*unit->GetSceneNode());
			unit->GetSceneNode()->SetPosition(feet);
			return unit;
		}

		MovementInfo packet(const Vector3& position, const uint32 flags, const GameTime timestamp, const Vector3& jumpVelocity = Vector3::Zero)
		{
			MovementInfo info;
			info.position = position;
			info.facing = Radian(0.0f);  // facing +X
			info.movementFlags = flags;
			info.timestamp = timestamp;
			info.jumpVelocity = jumpVelocity;
			return info;
		}
	}

	TEST_CASE("a remote player running into a wall is never drawn inside it", "[movement][remote]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		// Wall facing -X at x = 3.
		constexpr float wallX = 3.0f;
		TriangleCollidable floor;
		addFloor(floor);
		TriangleCollidable wall;
		addQuad(wall,
			Vector3(wallX, 0.0f, -20.0f),
			Vector3(wallX, 5.0f, -20.0f),
			Vector3(wallX, 5.0f, 20.0f),
			Vector3(wallX, 0.0f, 20.0f));
		scene.SetCollidables({ &floor, &wall });

		const Vector3 start(0.0f, kFloorGap, 0.0f);
		auto remote = createRemote(scene, net, project, start);

		// The owner starts running at the wall; it stops there and keeps holding forward. The
		// observer only learns where it actually is from the next heartbeat 500 ms later.
		remote->EnqueueRemoteMovement(packet(start, movement_flags::Forward, 1000));

		const float blockedX = wallX - kRadius;
		float maxX = start.x;
		for (int frame = 0; frame < 90; ++frame)
		{
			if (frame == 30)
			{
				remote->EnqueueRemoteMovement(packet(Vector3(blockedX - 0.01f, kFloorGap, 0.0f), movement_flags::Forward, 1500));
			}
			if (frame == 60)
			{
				remote->EnqueueRemoteMovement(packet(Vector3(blockedX - 0.01f, kFloorGap, 0.0f), movement_flags::Forward, 2000));
			}

			remote->UpdateRemoteMovement(kFrame);
			maxX = std::max(maxX, remote->GetPosition().x);
		}

		const Vector3 pos = remote->GetPosition();
		INFO("furthest x = " << maxX << ", capsule touches the wall at x = " << blockedX);
		INFO("final = (" << pos.x << ", " << pos.y << ", " << pos.z << ")");

		CHECK(maxX <= blockedX + 0.01f);
		CHECK(pos.x == Approx(blockedX).margin(0.05f));
	}

	TEST_CASE("a remote player sliding along a wall stays outside it", "[movement][remote]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		constexpr float wallX = 3.0f;
		TriangleCollidable floor;
		addFloor(floor);
		TriangleCollidable wall;
		addQuad(wall,
			Vector3(wallX, 0.0f, -20.0f),
			Vector3(wallX, 5.0f, -20.0f),
			Vector3(wallX, 5.0f, 20.0f),
			Vector3(wallX, 0.0f, 20.0f));
		scene.SetCollidables({ &floor, &wall });

		// Already touching the wall and running forward + strafing right: the owner slides along +Z.
		const float blockedX = wallX - kRadius;
		const Vector3 start(blockedX - 0.01f, kFloorGap, 0.0f);
		auto remote = createRemote(scene, net, project, start);
		remote->EnqueueRemoteMovement(packet(start, movement_flags::Forward | movement_flags::StrafeRight, 1000));

		float maxX = start.x;
		for (int frame = 0; frame < 30; ++frame)
		{
			remote->UpdateRemoteMovement(kFrame);
			maxX = std::max(maxX, remote->GetPosition().x);
		}

		const Vector3 pos = remote->GetPosition();
		INFO("furthest x = " << maxX << ", final = (" << pos.x << ", " << pos.y << ", " << pos.z << ")");

		CHECK(maxX <= blockedX + 0.01f);
		CHECK(pos.z > 0.5f);
	}

	TEST_CASE("a remote player jumping onto a box is never drawn sinking into it", "[movement][remote]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		// Box 1 m high from x = 2 onwards.
		constexpr float boxX = 2.0f;
		constexpr float boxTop = 1.0f;
		TriangleCollidable floor;
		addFloor(floor);
		TriangleCollidable box;
		addQuad(box,
			Vector3(boxX, 0.0f, -20.0f),
			Vector3(boxX, boxTop, -20.0f),
			Vector3(boxX, boxTop, 20.0f),
			Vector3(boxX, 0.0f, 20.0f));
		addQuad(box,
			Vector3(boxX, boxTop, -20.0f),
			Vector3(15.0f, boxTop, -20.0f),
			Vector3(15.0f, boxTop, 20.0f),
			Vector3(boxX, boxTop, 20.0f));
		scene.SetCollidables({ &floor, &box });

		const Vector3 start(0.0f, kFloorGap, 0.0f);
		auto remote = createRemote(scene, net, project, start);

		// A running jump: it clears the front edge and lands on top after ~0.66 s. The land
		// packet reaches the observer with a delay, at 0.9 s.
		remote->EnqueueRemoteMovement(packet(start, movement_flags::Forward | movement_flags::Falling, 1000, Vector3(kRunSpeed, 8.0f, 0.0f)));

		float lowestOverBox = 100.0f;
		for (int frame = 0; frame < 90; ++frame)
		{
			if (frame == 54)
			{
				const Vector3 landed(kRunSpeed * 0.66f + 0.3f, boxTop + kFloorGap, 0.0f);
				remote->EnqueueRemoteMovement(packet(landed, movement_flags::Forward, 1900));
			}

			remote->UpdateRemoteMovement(kFrame);

			// Once the whole capsule is above the box, nothing may be drawn below its top.
			const Vector3 pos = remote->GetPosition();
			if (pos.x > boxX + kRadius)
			{
				lowestOverBox = std::min(lowestOverBox, pos.y);
			}
		}

		INFO("lowest feet height above the box = " << lowestOverBox << ", box top = " << boxTop);
		CHECK(lowestOverBox >= boxTop - 0.01f);
	}
}

namespace mmo
{
	TEST_CASE("a remote player catching up around a wall end does not cut through it", "[movement][remote]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		// A thin wall at x = 1 that ends at z = 0.1. The owner went around its end and stopped
		// on the far side; the observer's copy is still on the near side and has to catch up.
		constexpr float wallX = 1.0f;
		constexpr float wallEndZ = 0.1f;
		TriangleCollidable floor;
		addFloor(floor);
		TriangleCollidable wall;
		addQuad(wall,
			Vector3(wallX, 0.0f, wallEndZ),
			Vector3(wallX, 5.0f, wallEndZ),
			Vector3(wallX, 5.0f, 10.0f),
			Vector3(wallX, 0.0f, 10.0f));
		scene.SetCollidables({ &floor, &wall });

		const Vector3 start(0.0f, kFloorGap, 0.0f);
		auto remote = createRemote(scene, net, project, start);
		remote->EnqueueRemoteMovement(packet(start, 0, 1000));
		remote->UpdateRemoteMovement(kFrame);

		// The straight line to the owner's position grazes the wall end.
		const Vector3 target(2.0f, kFloorGap, -0.2f);
		remote->EnqueueRemoteMovement(packet(target, 0, 1100));

		float closest = 100.0f;
		for (int frame = 0; frame < 60; ++frame)
		{
			remote->UpdateRemoteMovement(kFrame);

			const Vector3 pos = remote->GetPosition();
			const float dx = pos.x - wallX;
			const float dz = std::max(0.0f, wallEndZ - pos.z);
			closest = std::min(closest, std::sqrt(dx * dx + dz * dz));
		}

		const Vector3 pos = remote->GetPosition();
		INFO("closest approach of the capsule axis to the wall = " << closest << ", radius = " << kRadius);
		INFO("final = (" << pos.x << ", " << pos.y << ", " << pos.z << ")");

		CHECK(closest >= kRadius - 0.01f);
		CHECK((pos - target).GetLength() < 0.1f);
	}
}
