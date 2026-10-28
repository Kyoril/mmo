#include "catch.hpp"
#include "test_helpers/test_scene.h"
#include "test_helpers/test_net_client.h"
#include "test_helpers/test_game_unit.h"
#include "test_helpers/triangle_collidable.h"
#include "client_data/project.h"
#include "math/vector3.h"
#include "math/capsule.h"
#include "math/collision_shape.h"
#include "game/movement_type.h"
#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace
	{
		void addQuad(TriangleCollidable& collidable, const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d)
		{
			collidable.AddTriangle(a, b, c);
			collidable.AddTriangle(a, c, d);
		}

		/// @brief Horizontal distance a plane of the given inclination covers over the given rise.
		float runForRise(const float degrees, const float rise)
		{
			return rise / std::tan(degrees * 3.1415926535f / 180.0f);
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Wall climbing on unwalkable slopes
	// ─────────────────────────────────────────────────────────────────────────
	// Geometry: a flat floor plus the inside corner of two planes inclined at 50°,
	// which is steeper than the walkable limit (CollisionWalkableFloorY = 0.71,
	// i.e. ~44.8°), so neither plane may carry the unit.
	//
	// The unit is dropped above the corner while the player holds a diagonal run
	// input straight into it. Falling against geometry may only ever deflect the
	// movement sideways or downwards — a falling unit that is not jumping must
	// never gain upward velocity from the surfaces it bounces off, otherwise the
	// run input is converted into climbing speed and the player rides up the cliff.
	// ─────────────────────────────────────────────────────────────────────────
	TEST_CASE("falling into a steep inside corner must not produce upward velocity", "[movement][wall][slope]")
	{
		// Arrange
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		constexpr float slopeDegrees = 50.0f;
		constexpr float slopeHeight = 400.0f;
		const float slopeRun = runForRise(slopeDegrees, slopeHeight);

		// Sanity: the test geometry really is too steep to stand on.
		REQUIRE_FALSE(IsCollisionFaceWalkable(
			Vector3(-1.0f, 0.0f, 5.0f),
			Vector3(1.0f, 0.0f, 5.0f),
			Vector3(1.0f, slopeHeight, 5.0f + slopeRun)));

		TriangleCollidable floor;
		addQuad(floor,
			Vector3(-50.0f, 0.0f, -50.0f),
			Vector3(-50.0f, 0.0f, 50.0f),
			Vector3(50.0f, 0.0f, 50.0f),
			Vector3(50.0f, 0.0f, -50.0f));

		// Slope facing -Z, rising towards +Z from the line z = 5.
		TriangleCollidable slopeA;
		addQuad(slopeA,
			Vector3(-500.0f, 0.0f, 5.0f),
			Vector3(500.0f, 0.0f, 5.0f),
			Vector3(500.0f, slopeHeight, 5.0f + slopeRun),
			Vector3(-500.0f, slopeHeight, 5.0f + slopeRun));

		// Slope facing -X, rising towards +X from the line x = 5.
		TriangleCollidable slopeB;
		addQuad(slopeB,
			Vector3(5.0f, 0.0f, -500.0f),
			Vector3(5.0f, 0.0f, 500.0f),
			Vector3(5.0f + slopeRun, slopeHeight, 500.0f),
			Vector3(5.0f + slopeRun, slopeHeight, -500.0f));

		scene.SetCollidables({ &floor, &slopeA, &slopeB });

		auto unit = std::make_shared<TestGameUnit>(scene, net, project, 0);

		const float runSpeed = 7.0f;
		unit->SetSpeed(movement_type::Run, runSpeed);
		unit->SetSpeed(movement_type::Walk, runSpeed);
		unit->SetSpeed(movement_type::Backwards, runSpeed * 0.5f);

		const float radius = 0.25f;
		Capsule capsule(
			Vector3(0.0f, radius, 0.0f),
			Vector3(0.0f, radius + 1.8f, 0.0f),
			radius);
		unit->SetColliderForTest(capsule);

		const Vector3 startPos(2.0f, 6.0f, 2.0f);
		SceneNode* node = unit->GetSceneNode();
		if (node)
		{
			node->SetPosition(startPos);
		}

		// Act: fall into the corner while holding the run input into it. The unit never jumps.
		UnitMovement* movement = unit->GetUnitMovement();
		REQUIRE(movement != nullptr);

		const Vector3 inputDir = Vector3(1.0f, 0.0f, 1.0f).NormalizedCopy();

		float maxUpwardVelocity = 0.0f;
		int maxUpwardVelocityStep = -1;
		for (int step = 0; step < 1800; ++step)
		{
			unit->AddInputVector(inputDir);
			movement->Tick(1.0f / 60.0f);

			const float verticalVelocity = movement->GetVelocity().y;
			if (verticalVelocity > maxUpwardVelocity)
			{
				maxUpwardVelocity = verticalVelocity;
				maxUpwardVelocityStep = step;
			}
		}

		const Vector3 pos = unit->GetPosition();

		INFO("pos = (" << pos.x << ", " << pos.y << ", " << pos.z << ")");
		INFO("maxUpwardVelocity = " << maxUpwardVelocity << " at step " << maxUpwardVelocityStep);

		CHECK(!std::isnan(pos.x));
		CHECK(!std::isnan(pos.y));
		CHECK(!std::isnan(pos.z));

		// The unit never jumped, so no tick may ever show upward velocity: deflecting off a
		// too-steep surface must not turn run speed into climbing speed.
		CHECK(maxUpwardVelocity <= 0.01f);

		// And it must end up back on the floor rather than parked on the cliff face.
		CHECK(pos.y < 0.5f);
	}
}
