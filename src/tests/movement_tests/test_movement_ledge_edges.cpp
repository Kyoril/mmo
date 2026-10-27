#include "catch.hpp"
#include "test_helpers/test_scene.h"
#include "test_helpers/test_net_client.h"
#include "test_helpers/test_game_unit.h"
#include "test_helpers/triangle_collidable.h"
#include "client_data/project.h"
#include "math/vector3.h"
#include "math/capsule.h"
#include "game/movement_type.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace mmo
{
	namespace
	{
		constexpr float tickSeconds = 1.0f / 60.0f;
		constexpr float capsuleRadius = 0.35f;

		void addQuad(TriangleCollidable& collidable, const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d)
		{
			collidable.AddTriangle(a, b, c);
			collidable.AddTriangle(a, c, d);
		}

		float runForRise(const float degrees, const float rise)
		{
			return rise / std::tan(degrees * 3.1415926535f / 180.0f);
		}

		std::shared_ptr<TestGameUnit> createUnit(TestScene& scene, TestNetClient& net, proto_client::Project& project, const Vector3& feet, const float speed)
		{
			auto unit = std::make_shared<TestGameUnit>(scene, net, project, 0);
			unit->SetSpeed(movement_type::Run, speed);
			unit->SetSpeed(movement_type::Walk, speed);
			unit->SetSpeed(movement_type::Backwards, speed * 0.5f);

			const Capsule capsule(
				Vector3(0.0f, capsuleRadius, 0.0f),
				Vector3(0.0f, capsuleRadius + 1.8f, 0.0f),
				capsuleRadius);
			unit->SetColliderForTest(capsule);
			unit->GetSceneNode()->SetPosition(feet);
			return unit;
		}

		/// @brief Counts how often a unit switches between being supported and falling.
		struct FallTracker
		{
			int fallStarts = 0;
			int landings = 0;
			bool wasFalling = false;

			void Sample(const UnitMovement& movement)
			{
				const bool falling = movement.IsFalling();
				if (falling && !wasFalling)
				{
					++fallStarts;
				}
				else if (!falling && wasFalling)
				{
					++landings;
				}
				wasFalling = falling;
			}
		};

		/// @brief Ticks until the unit rests on the ground, so tests start from a settled state.
		void settle(TestGameUnit& unit)
		{
			UnitMovement& movement = *unit.GetUnitMovement();
			for (int i = 0; i < 30; ++i)
			{
				movement.Tick(tickSeconds);
			}
		}

		/// @brief A raised platform with a sharp 90° edge: top at y = height for z <= 0, a vertical
		/// face at z = 0 and the lower floor at y = 0 for z >= 0.
		struct LedgeScene
		{
			TriangleCollidable top;
			TriangleCollidable face;
			TriangleCollidable lower;
			TriangleCollidable mesh;

			explicit LedgeScene(const float height)
			{
				// The same triangles once more as a single mesh, the way level geometry arrives
				// from an Entity: every face of the ledge is part of one collidable. The wall
				// comes first so that a sweep which keeps only the first triangle picks it.
				addQuad(mesh,
					Vector3(-20.0f, 0.0f, 0.0f),
					Vector3(-20.0f, height, 0.0f),
					Vector3(20.0f, height, 0.0f),
					Vector3(20.0f, 0.0f, 0.0f));
				addQuad(mesh,
					Vector3(-20.0f, height, -20.0f),
					Vector3(-20.0f, height, 0.0f),
					Vector3(20.0f, height, 0.0f),
					Vector3(20.0f, height, -20.0f));
				addQuad(mesh,
					Vector3(-20.0f, 0.0f, 0.0f),
					Vector3(-20.0f, 0.0f, 40.0f),
					Vector3(20.0f, 0.0f, 40.0f),
					Vector3(20.0f, 0.0f, 0.0f));

				addQuad(top,
					Vector3(-20.0f, height, -20.0f),
					Vector3(-20.0f, height, 0.0f),
					Vector3(20.0f, height, 0.0f),
					Vector3(20.0f, height, -20.0f));
				addQuad(face,
					Vector3(-20.0f, 0.0f, 0.0f),
					Vector3(-20.0f, height, 0.0f),
					Vector3(20.0f, height, 0.0f),
					Vector3(20.0f, 0.0f, 0.0f));
				addQuad(lower,
					Vector3(-20.0f, 0.0f, 0.0f),
					Vector3(-20.0f, 0.0f, 40.0f),
					Vector3(20.0f, 0.0f, 40.0f),
					Vector3(20.0f, 0.0f, 0.0f));
			}

			void Use(TestScene& scene, const bool singleMesh)
			{
				if (singleMesh)
				{
					scene.SetCollidables({ &mesh });
				}
				else
				{
					scene.SetCollidables({ &top, &face, &lower });
				}
			}
		};
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Ledge edges
	// ─────────────────────────────────────────────────────────────────────────
	// A capsule whose centre has passed a sharp edge still rests on it with the
	// rounded bottom. The floor it rests on is the flat top face: whether it may
	// stand there has to be judged by that face, not by the direction from the
	// edge line to the sphere centre, which tilts towards horizontal as the unit
	// moves outwards and flips walking into falling every few frames.
	// ─────────────────────────────────────────────────────────────────────────
	TEST_CASE("a unit resting on a sharp ledge edge stays supported", "[movement][ledge]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		constexpr float height = 2.0f;
		LedgeScene ledge(height);
		const bool singleMesh = GENERATE(false, true);
		ledge.Use(scene, singleMesh);

		// Centre 0.2 m past the edge: the sphere still overlaps the platform by 0.15 m, and the
		// edge-to-centre direction is tilted ~55° from vertical (steeper than walkable).
		auto unit = createUnit(scene, net, project, Vector3(0.0f, height + 0.01f, 0.2f), 7.0f);
		settle(*unit);
		UnitMovement& movement = *unit->GetUnitMovement();
		REQUIRE_FALSE(movement.IsFalling());

		FallTracker tracker;
		for (int i = 0; i < 180; ++i)
		{
			movement.Tick(tickSeconds);
			tracker.Sample(movement);
		}

		const Vector3 pos = unit->GetPosition();
		INFO("singleMesh = " << singleMesh);
		INFO("pos = (" << pos.x << ", " << pos.y << ", " << pos.z << ")");
		INFO("fallStarts = " << tracker.fallStarts << ", landings = " << tracker.landings);

		// The bottom sphere rests on the edge line, so the feet sit where the sphere geometry puts
		// them (below the platform top) plus the regular floor gap — not higher, as they would with
		// a floor distance that is off, and not lower, as they would if the capsule sank into it.
		const float sphereCentreAboveEdge = std::sqrt(capsuleRadius * capsuleRadius - 0.2f * 0.2f);
		const float restingFeetY = height + sphereCentreAboveEdge - capsuleRadius;
		INFO("expected feet at " << restingFeetY << " + floor gap");

		CHECK(tracker.fallStarts == 0);
		CHECK(pos.y >= restingFeetY);
		CHECK(pos.y <= restingFeetY + 0.03f);
		CHECK(pos.z == Approx(0.2f).margin(0.02f));
	}

	// The floor sweep and the line trace that takes over near an edge must agree on how far the
	// floor is, or the unit rides at one height on the open floor and another near the edge and
	// pops between them every time it walks over one.
	TEST_CASE("walking up to a ledge keeps the same ride height up to the edge", "[movement][ledge]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		constexpr float height = 2.0f;
		LedgeScene ledge(height);
		const bool singleMesh = GENERATE(false, true);
		ledge.Use(scene, singleMesh);

		auto unit = createUnit(scene, net, project, Vector3(0.0f, height, -3.0f), 2.5f);
		settle(*unit);
		UnitMovement& movement = *unit->GetUnitMovement();

		float minY = unit->GetPosition().y;
		float maxY = minY;
		// Up to the point where the capsule centre is above the edge, the bottom sphere is still
		// carried by the top face directly beneath it. Past that it rolls over the edge line.
		for (int i = 0; i < 240 && !movement.IsFalling() && unit->GetPosition().z <= 0.0f; ++i)
		{
			unit->AddInputVector(Vector3(0.0f, 0.0f, 1.0f));
			movement.Tick(tickSeconds);
			if (!movement.IsFalling() && unit->GetPosition().z <= 0.0f)
			{
				minY = std::min(minY, unit->GetPosition().y);
				maxY = std::max(maxY, unit->GetPosition().y);
			}
		}

		INFO("singleMesh = " << singleMesh);
		INFO("ride height between " << minY << " and " << maxY);

		// The floor band is MIN_FLOOR_DIST..MAX_FLOOR_DIST (0.019..0.024 m) above the surface.
		CHECK(minY >= height);
		CHECK(maxY <= height + 0.03f);
		CHECK(maxY - minY < 0.01f);
	}

	TEST_CASE("walking off a sharp ledge falls exactly once", "[movement][ledge]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		constexpr float height = 2.0f;
		LedgeScene ledge(height);
		const bool singleMesh = GENERATE(false, true);
		ledge.Use(scene, singleMesh);

		// Walking speed: a slow approach dwells on the edge for many ticks.
		const float speed = GENERATE(2.5f, 7.0f);
		auto unit = createUnit(scene, net, project, Vector3(0.0f, height, -3.0f), speed);
		settle(*unit);
		UnitMovement& movement = *unit->GetUnitMovement();

		FallTracker tracker;
		float lowestYAfterLeaving = height;
		bool roseAfterLeaving = false;
		for (int i = 0; i < 240; ++i)
		{
			unit->AddInputVector(Vector3(0.0f, 0.0f, 1.0f));
			movement.Tick(tickSeconds);
			tracker.Sample(movement);

			const float y = unit->GetPosition().y;
			if (movement.IsFalling())
			{
				// Once off the edge the unit may only go down, never bounce back up onto it.
				if (y > lowestYAfterLeaving + 0.01f)
				{
					roseAfterLeaving = true;
				}
				lowestYAfterLeaving = std::min(lowestYAfterLeaving, y);
			}
		}

		const Vector3 pos = unit->GetPosition();
		INFO("singleMesh = " << singleMesh);
		INFO("speed = " << speed);
		INFO("pos = (" << pos.x << ", " << pos.y << ", " << pos.z << ")");
		INFO("fallStarts = " << tracker.fallStarts << ", landings = " << tracker.landings);

		CHECK(tracker.fallStarts == 1);
		CHECK(tracker.landings == 1);
		CHECK_FALSE(roseAfterLeaving);
		CHECK(pos.y == Approx(0.0f).margin(0.03f));
		CHECK(pos.z > 1.0f);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Steep slopes
	// ─────────────────────────────────────────────────────────────────────────
	TEST_CASE("a unit standing on a steep walkable slope stays put", "[movement][slope]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		// 40° is walkable (limit ~44.8°) but steep. The plane rises towards +Z through the origin.
		constexpr float slopeDegrees = 40.0f;
		const float run = runForRise(slopeDegrees, 30.0f);
		TriangleCollidable slope;
		addQuad(slope,
			Vector3(-20.0f, 0.0f, 0.0f),
			Vector3(-20.0f, 30.0f, run),
			Vector3(20.0f, 30.0f, run),
			Vector3(20.0f, 0.0f, 0.0f));
		scene.SetCollidables({ &slope });

		const float z = 10.0f;
		const float y = z * std::tan(slopeDegrees * 3.1415926535f / 180.0f);
		auto unit = createUnit(scene, net, project, Vector3(0.0f, y + 0.05f, z), 7.0f);
		settle(*unit);
		UnitMovement& movement = *unit->GetUnitMovement();
		const Vector3 settled = unit->GetPosition();

		FallTracker tracker;
		for (int i = 0; i < 180; ++i)
		{
			movement.Tick(tickSeconds);
			tracker.Sample(movement);
		}

		const Vector3 pos = unit->GetPosition();
		INFO("settled = (" << settled.x << ", " << settled.y << ", " << settled.z << ")");
		INFO("pos = (" << pos.x << ", " << pos.y << ", " << pos.z << ")");
		INFO("fallStarts = " << tracker.fallStarts);

		CHECK(tracker.fallStarts == 0);
		CHECK_FALSE(movement.IsFalling());
		CHECK((pos - settled).GetLength() < 0.02f);
	}

	TEST_CASE("a unit standing at the brink of a steep cliff stays supported", "[movement][slope][ledge]")
	{
		proto_client::Project project;
		TestScene scene;
		TestNetClient net;

		// Plateau at y = 10 for z <= 0, dropping away along a 60° (unwalkable) face towards +Z.
		constexpr float height = 10.0f;
		const float run = runForRise(60.0f, height);
		TriangleCollidable plateau;
		addQuad(plateau,
			Vector3(-20.0f, height, -20.0f),
			Vector3(-20.0f, height, 0.0f),
			Vector3(20.0f, height, 0.0f),
			Vector3(20.0f, height, -20.0f));
		TriangleCollidable cliff;
		addQuad(cliff,
			Vector3(-20.0f, height, 0.0f),
			Vector3(-20.0f, 0.0f, run),
			Vector3(20.0f, 0.0f, run),
			Vector3(20.0f, height, 0.0f));
		scene.SetCollidables({ &plateau, &cliff });

		// Centre 0.2 m past the brink.
		auto unit = createUnit(scene, net, project, Vector3(0.0f, height + 0.01f, 0.2f), 7.0f);
		settle(*unit);
		UnitMovement& movement = *unit->GetUnitMovement();
		REQUIRE_FALSE(movement.IsFalling());

		FallTracker tracker;
		for (int i = 0; i < 180; ++i)
		{
			movement.Tick(tickSeconds);
			tracker.Sample(movement);
		}

		const Vector3 pos = unit->GetPosition();
		INFO("pos = (" << pos.x << ", " << pos.y << ", " << pos.z << ")");
		INFO("fallStarts = " << tracker.fallStarts);

		// Resting on the brink line exactly like on a 90° ledge.
		const float restingFeetY = height + std::sqrt(capsuleRadius * capsuleRadius - 0.2f * 0.2f) - capsuleRadius;

		CHECK(tracker.fallStarts == 0);
		CHECK(pos.y >= restingFeetY);
		CHECK(pos.y <= restingFeetY + 0.03f);
	}
}
