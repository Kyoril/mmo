/// @file test_movement_advanced.cpp
/// @brief Advanced movement unit tests covering:
///   - Wall-parallel speed correctness (the wall-slide speed bug)
///   - Wall-perpendicular approach (unit stops, doesn't slide)
///   - Wall-angle correctness: parallel speed = cos(approachAngle) * runSpeed
///   - Direction change at wall (A/D turn while sliding): momentum stops cleanly
///   - Gravity / falling: unit falls off edge, reaches terminal-ish velocity
///   - Falling distance: free-fall displacement matches kinematics within tolerance
///   - Land after fall: unit stops at floor level after falling onto it
///   - Forward + wall slide is not faster than open-plane travel in same direction
///
/// Geometry convention: floor at Y=0, normal +Y. Wall at X=Xw, normal +X (facing -X).
/// All capsules: radius=0.35 m, half-height=0.9 m, run speed=7 m/s.
/// Timestep: 1/60 s (60 Hz).

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
// ─────────────────────────────────────────────────────────────────────────────
// Shared test fixtures
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
	constexpr float kRunSpeed   = 7.0f;
	constexpr float kDt         = 1.0f / 60.0f;
	constexpr float kRadius     = 0.35f;
	constexpr float kHalfHeight = 0.9f;

	/// Standard capsule with feet at `pos`.
	Capsule MakeCapsule(const Vector3& pos)
	{
		return Capsule(
			pos + Vector3(0.0f, kRadius, 0.0f),
			pos + Vector3(0.0f, kRadius + kHalfHeight * 2.0f, 0.0f),
			kRadius);
	}

	/// Large flat floor at Y=0, covering [-100,100] in XZ.
	void AddFloor(TriangleCollidable& t)
	{
		t.AddTriangle(
			Vector3(-100.f, 0.f, -100.f),
			Vector3( 100.f, 0.f, -100.f),
			Vector3(-100.f, 0.f,  100.f));
		t.AddTriangle(
			Vector3( 100.f, 0.f, -100.f),
			Vector3( 100.f, 0.f,  100.f),
			Vector3(-100.f, 0.f,  100.f));
	}

	/// Vertical wall at X=wallX, normal +X (facing -X toward origin).
	/// Covers Y=[0,20], Z=[-50,50].
	void AddWallAtX(TriangleCollidable& t, float wallX)
	{
		// CCW winding viewed from -X side → normal = (+1, 0, 0)
		t.AddTriangle(
			Vector3(wallX,  0.f, -50.f),
			Vector3(wallX, 20.f, -50.f),
			Vector3(wallX,  0.f,  50.f));
		t.AddTriangle(
			Vector3(wallX,  0.f,  50.f),
			Vector3(wallX, 20.f, -50.f),
			Vector3(wallX, 20.f,  50.f));
	}

	/// Configure run/walk/back speeds on a unit.
	void SetSpeeds(TestGameUnit& u)
	{
		u.SetSpeed(movement_type::Run,       kRunSpeed);
		u.SetSpeed(movement_type::Walk,      kRunSpeed);
		u.SetSpeed(movement_type::Backwards, kRunSpeed * 0.5f);
	}

	/// Create and position a unit at `pos`.
	std::shared_ptr<TestGameUnit> MakeUnit(
		TestScene& scene, TestNetClient& net,
		proto_client::Project& project, const Vector3& pos)
	{
		auto unit = std::make_shared<TestGameUnit>(scene, net, project, 0);
		SetSpeeds(*unit);
		unit->SetColliderForTest(MakeCapsule(pos));
		if (SceneNode* n = unit->GetSceneNode()) { n->SetPosition(pos); }
		return unit;
	}

	/// Run unit for `steps` ticks with constant input, return final speed.
	float RunAndGetSpeed(TestGameUnit& unit, const Vector3& input, int steps)
	{
		SimulateMovement(unit, input, steps, kDt);
		const Vector3 v = unit.GetUnitMovement()->GetVelocity();
		return std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
	}

	/// Run unit for `steps` ticks, returning peak speed seen each tick.
	float RunAndGetPeakSpeed(TestGameUnit& unit, const Vector3& input, int steps)
	{
		float peak = 0.f;
		SimulateMovement(unit, input, steps, kDt,
			[&](int, const Vector3&, const Vector3& v)
			{
				const float s = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
				peak = std::max(peak, s);
			});
		return peak;
	}
} // anonymous namespace


// ─────────────────────────────────────────────────────────────────────────────
// 1. Wall-parallel speed: approaching at a shallow angle
//    forward = (sin θ, 0, cos θ), wall normal = +X
//    expected parallel Z-speed = cos(θ) * runSpeed
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("wall-slide: parallel speed equals cosine-fraction of run speed", "[movement][wall][speed]")
{
	// Approach angle from wall normal (larger = shallower approach, more Z speed)
	const float angleDeg = 75.f;  // close to the angle in the real-world bug report
	const float angleRad = angleDeg * (3.14159265f / 180.f);
	// input vector: sin(angle) toward wall (+X), cos(angle) along wall (+Z)
	const Vector3 input = Vector3(std::sin(angleRad), 0.f, std::cos(angleRad));

	proto_client::Project project;
	TestScene scene;
	TestNetClient net;

	TriangleCollidable floor, wall;
	AddFloor(floor);
	AddWallAtX(wall, 10.f);
	scene.SetCollidables({ &floor, &wall });

	// Start well away from wall — unit needs ~10m to reach it and settle
	auto unit = MakeUnit(scene, net, project, Vector3(0.f, 0.f, 0.f));

	// Run long enough to hit wall and fully settle (3 s at 60 Hz)
	constexpr int steps = 180;
	SimulateMovement(*unit, input, steps, kDt);

	const Vector3 vel   = unit->GetUnitMovement()->GetVelocity();
	const float   speedZ = std::abs(vel.z);
	const float   speedX = std::abs(vel.x);
	const float   speed  = std::sqrt(vel.x*vel.x + vel.y*vel.y + vel.z*vel.z);

	// Expected: wall strips X component, leaving only Z = cos(angle)*runSpeed
	const float expectedZ = std::cos(angleRad) * kRunSpeed;

	INFO("angle=" << angleDeg << "° input=(" << input.x << ",0," << input.z << ")");
	INFO("vel=(" << vel.x << "," << vel.y << "," << vel.z << ") speed=" << speed);
	INFO("speedZ=" << speedZ << " expectedZ=" << expectedZ);
	INFO("speedX=" << speedX << " (should be ~0)");

	CHECK(!std::isnan(vel.x));
	CHECK(!std::isnan(vel.z));
	// Unit pressed against wall — X component must be near zero
	CHECK(speedX < 0.5f);
	// Z speed must match cos(approach angle) * runSpeed within 10%
	CHECK(speedZ == Approx(expectedZ).epsilon(0.10f));
	// Total speed must not exceed runSpeed
	CHECK(speed <= kRunSpeed * 1.05f);
}


// ─────────────────────────────────────────────────────────────────────────────
// 2. Wall-perpendicular approach: unit stops dead, no residual slide
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("wall-slide: perpendicular approach causes unit to stop", "[movement][wall][speed]")
{
	proto_client::Project project;
	TestScene scene;
	TestNetClient net;

	TriangleCollidable floor, wall;
	AddFloor(floor);
	AddWallAtX(wall, 5.f);
	scene.SetCollidables({ &floor, &wall });

	auto unit = MakeUnit(scene, net, project, Vector3(0.f, 0.f, 0.f));

	// Drive straight into wall (+X), 2 s
	SimulateMovement(*unit, Vector3(1.f, 0.f, 0.f), 120, kDt);

	const Vector3 vel   = unit->GetUnitMovement()->GetVelocity();
	const float   speed = std::sqrt(vel.x*vel.x + vel.y*vel.y + vel.z*vel.z);
	const Vector3 pos   = unit->GetPosition();

	INFO("pos=(" << pos.x << "," << pos.y << "," << pos.z << ")");
	INFO("vel=(" << vel.x << "," << vel.y << "," << vel.z << ") speed=" << speed);

	CHECK(!std::isnan(pos.x));
	// Unit must not pass through wall
	CHECK(pos.x <= 5.5f);
	// Speed must be near zero — perpendicular wall, nothing to slide along
	CHECK(speed < 0.5f);
}


// ─────────────────────────────────────────────────────────────────────────────
// 3. Wall-slide not faster than open-plane travel
//    The original bug: sliding along a wall at a shallow angle was faster than
//    running in the same direction (Z) on an open plane.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("wall-slide: distance along wall not greater than open-plane distance", "[movement][wall][speed]")
{
	const float angleDeg = 80.f;  // very shallow — worst-case for speed boost
	const float angleRad = angleDeg * (3.14159265f / 180.f);
	const Vector3 input  = Vector3(std::sin(angleRad), 0.f, std::cos(angleRad));

	constexpr int steps   = 240;   // 4 s — enough to run from origin to wall and then slide
	const float wallX     = 8.f;   // close enough to reach quickly

	proto_client::Project project1, project2;
	TestScene sceneFlat, sceneWall;
	TestNetClient net1, net2;

	// Flat plane (no wall)
	TriangleCollidable floorOnly;
	AddFloor(floorOnly);
	sceneFlat.SetCollidables({ &floorOnly });

	auto unitFlat = MakeUnit(sceneFlat, net1, project1, Vector3(0.f, 0.f, 0.f));
	SimulateMovement(*unitFlat, input, steps, kDt);
	const float zFlat = unitFlat->GetPosition().z;

	// Plane + wall
	TriangleCollidable floorWall, wall;
	AddFloor(floorWall);
	AddWallAtX(wall, wallX);
	sceneWall.SetCollidables({ &floorWall, &wall });

	auto unitWall = MakeUnit(sceneWall, net2, project2, Vector3(0.f, 0.f, 0.f));
	SimulateMovement(*unitWall, input, steps, kDt);
	const float zWall = unitWall->GetPosition().z;

	INFO("angle=" << angleDeg << "°");
	INFO("zFlat=" << zFlat << "  zWall=" << zWall);
	INFO("wall unit is " << (zWall > zFlat ? "FASTER" : "same/slower") << " by " << (zWall - zFlat) << " m");

	// Wall unit must not have travelled significantly more in Z than the free unit.
	// Allow 5% tolerance for rounding/frame-timing differences.
	CHECK(zWall <= zFlat * 1.05f);
}


// ─────────────────────────────────────────────────────────────────────────────
// 4. Direction reversal at wall: no backwards-glide momentum
//    Press into wall → turn 180° → unit should decelerate, not keep gliding.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("wall-slide: turning away from wall stops wall-parallel glide", "[movement][wall][momentum]")
{
	proto_client::Project project;
	TestScene scene;
	TestNetClient net;

	TriangleCollidable floor, wall;
	AddFloor(floor);
	AddWallAtX(wall, 5.f);
	scene.SetCollidables({ &floor, &wall });

	// Approach at 75° so we get a good wall-parallel slide going
	const float angleRad = 75.f * (3.14159265f / 180.f);
	const Vector3 slideInput = Vector3(std::sin(angleRad), 0.f, std::cos(angleRad));

	auto unit = MakeUnit(scene, net, project, Vector3(0.f, 0.f, 0.f));

	// Phase 1: build up wall-parallel slide (1.5 s)
	SimulateMovement(*unit, slideInput, 90, kDt);

	const Vector3 velAfterSlide = unit->GetUnitMovement()->GetVelocity();
	const float   slideSpeedZ   = std::abs(velAfterSlide.z);

	// Phase 2: reverse direction — drive -Z (away from wall direction)
	SimulateMovement(*unit, Vector3(0.f, 0.f, -1.f), 60, kDt);

	const Vector3 velFinal = unit->GetUnitMovement()->GetVelocity();
	const float   speedFinal = std::sqrt(velFinal.x*velFinal.x + velFinal.y*velFinal.y + velFinal.z*velFinal.z);

	INFO("slideSpeedZ after wall phase=" << slideSpeedZ);
	INFO("final vel=(" << velFinal.x << "," << velFinal.y << "," << velFinal.z << ") speed=" << speedFinal);

	// After 1 s of reverse input the unit must be moving in -Z, not still +Z
	CHECK(!std::isnan(velFinal.z));
	// The Z velocity must have reversed within 1 s
	CHECK(velFinal.z < 0.f);
	// And speed must be ≤ runSpeed — no compounding momentum
	CHECK(speedFinal <= kRunSpeed * 1.05f);
}


// ─────────────────────────────────────────────────────────────────────────────
// 5. Gravity: unit placed above floor accelerates downward and lands
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("gravity: unit dropped from height lands on floor", "[movement][gravity][falling]")
{
	proto_client::Project project;
	TestScene scene;
	TestNetClient net;

	TriangleCollidable floor;
	AddFloor(floor);
	scene.SetCollidables({ &floor });

	// Spawn unit 5 m above floor (feet at Y=5)
	const Vector3 startPos(0.f, 5.f, 0.f);
	auto unit = MakeUnit(scene, net, project, startPos);

	// Gravity = -9.8 * 2.0 = -19.6 m/s².  Time to fall 5 m: t = sqrt(2*5/19.6) ≈ 0.714 s
	// Simulate 2 s to be safe.
	float peakFallSpeed = 0.f;
	SimulateMovement(*unit, Vector3::Zero, 120, kDt,
		[&](int, const Vector3&, const Vector3& v)
		{
			peakFallSpeed = std::max(peakFallSpeed, std::abs(v.y));
		});

	const Vector3 pos = unit->GetPosition();
	const Vector3 vel = unit->GetUnitMovement()->GetVelocity();

	INFO("startY=5  finalY=" << pos.y << "  peakFallSpeed=" << peakFallSpeed);
	INFO("vel=(" << vel.x << "," << vel.y << "," << vel.z << ")");

	CHECK(!std::isnan(pos.y));
	// Must have landed on the floor
	CHECK(pos.y == Approx(0.f).margin(0.3f));
	// Must have actually fallen (peakFallSpeed > 0)
	CHECK(peakFallSpeed > 1.f);
	// After landing, vertical velocity must be near zero
	CHECK(std::abs(vel.y) < 1.f);
}


// ─────────────────────────────────────────────────────────────────────────────
// 6. Falling kinematics: displacement matches s = ½ g t²
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("gravity: free-fall displacement matches kinematic formula", "[movement][gravity][falling]")
{
	proto_client::Project project;
	TestScene scene;
	TestNetClient net;

	// No floor — unit falls freely.  Place a deep floor so it doesn't land
	// during the measurement window.
	TriangleCollidable floor;
	floor.AddTriangle(
		Vector3(-100.f, -500.f, -100.f),
		Vector3( 100.f, -500.f, -100.f),
		Vector3(-100.f, -500.f,  100.f));
	floor.AddTriangle(
		Vector3( 100.f, -500.f, -100.f),
		Vector3( 100.f, -500.f,  100.f),
		Vector3(-100.f, -500.f,  100.f));
	scene.SetCollidables({ &floor });

	// Start at Y=0 (relative), no floor nearby
	const Vector3 startPos(0.f, 100.f, 0.f);
	auto unit = MakeUnit(scene, net, project, startPos);

	// Measure fall over 0.5 s (30 ticks)
	constexpr int fallTicks = 30;
	const float   fallTime  = fallTicks * kDt;

	SimulateMovement(*unit, Vector3::Zero, fallTicks, kDt);

	const Vector3 pos       = unit->GetPosition();
	const float   fallDist  = startPos.y - pos.y;   // positive = downward

	// g = 9.8 * gravityScale(2.0) = 19.6 m/s²
	const float g        = 9.8f * 2.0f;
	const float expected = 0.5f * g * fallTime * fallTime;

	INFO("fallTime=" << fallTime << "s  fallDist=" << fallDist << "m  expected=" << expected << "m");
	INFO("pos.y=" << pos.y);

	CHECK(!std::isnan(pos.y));
	// Allow 15% tolerance: discrete integration + capsule radius offsets
	CHECK(fallDist == Approx(expected).epsilon(0.15f));
}


// ─────────────────────────────────────────────────────────────────────────────
// 7. Walking off an edge: unit falls after leaving floor boundary
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("gravity: unit walks off edge and falls", "[movement][gravity][falling]")
{
	proto_client::Project project;
	TestScene scene;
	TestNetClient net;

	// Small floor platform: Z in [0,3], X in [-5,5], at Y=0
	TriangleCollidable platform;
	platform.AddTriangle(
		Vector3(-5.f, 0.f, 0.f),
		Vector3( 5.f, 0.f, 0.f),
		Vector3(-5.f, 0.f, 3.f));
	platform.AddTriangle(
		Vector3( 5.f, 0.f, 0.f),
		Vector3( 5.f, 0.f, 3.f),
		Vector3(-5.f, 0.f, 3.f));

	// Landing floor far below at Y=-10
	TriangleCollidable landing;
	landing.AddTriangle(
		Vector3(-50.f, -10.f, -50.f),
		Vector3( 50.f, -10.f, -50.f),
		Vector3(-50.f, -10.f,  50.f));
	landing.AddTriangle(
		Vector3( 50.f, -10.f, -50.f),
		Vector3( 50.f, -10.f,  50.f),
		Vector3(-50.f, -10.f,  50.f));

	scene.SetCollidables({ &platform, &landing });

	// Start at back of platform, walk forward off the edge
	const Vector3 startPos(0.f, 0.f, 0.f);
	auto unit = MakeUnit(scene, net, project, startPos);

	// Walk forward (+Z) for 3 s — leaves platform edge at Z≈3 within ~0.5 s
	float minY = 0.f;
	SimulateMovement(*unit, Vector3(0.f, 0.f, 1.f), 180, kDt,
		[&](int, const Vector3& p, const Vector3&)
		{
			minY = std::min(minY, p.y);
		});

	const Vector3 pos = unit->GetPosition();
	INFO("finalPos=(" << pos.x << "," << pos.y << "," << pos.z << ")  minY=" << minY);

	CHECK(!std::isnan(pos.y));
	// Unit must have fallen significantly below the platform
	CHECK(minY < -1.f);
	// Final resting position must be on the landing floor
	CHECK(pos.y == Approx(-10.f).margin(0.5f));
}


// ─────────────────────────────────────────────────────────────────────────────
// 8. Jump: peak height is physically plausible
//    jumpImpulse is set by the game data; with gravityScale=2 we measure empirically.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("jump: peak height above floor is > 0 and speed bounded", "[movement][jump][gravity]")
{
	proto_client::Project project;
	TestScene scene;
	TestNetClient net;

	TriangleCollidable floor;
	AddFloor(floor);
	scene.SetCollidables({ &floor });

	const Vector3 startPos(0.f, 0.f, 0.f);
	auto unit = MakeUnit(scene, net, project, startPos);

	unit->Jump();  // request jump before first tick

	float peakY     = 0.f;
	float peakSpeed = 0.f;

	UnitMovement* mv = unit->GetUnitMovement();
	for (int i = 0; i < 180; ++i)
	{
		mv->Tick(kDt);
		const Vector3 p = unit->GetPosition();
		const Vector3 v = mv->GetVelocity();
		peakY     = std::max(peakY, p.y);
		peakSpeed = std::max(peakSpeed, std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z));
	}

	const Vector3 pos = unit->GetPosition();
	const Vector3 vel = mv->GetVelocity();

	INFO("peakY=" << peakY << "  finalY=" << pos.y);
	INFO("finalVel=(" << vel.x << "," << vel.y << "," << vel.z << ")");
	INFO("peakSpeed=" << peakSpeed);

	CHECK(!std::isnan(pos.y));
	// Must have left the floor
	CHECK(peakY > 0.3f);
	// Must have landed back (within 0.2 m of floor)
	CHECK(pos.y == Approx(0.f).margin(0.2f));
	// Speed must never have exploded
	CHECK(peakSpeed < 50.f);
	// After landing, vertical velocity near zero
	CHECK(std::abs(vel.y) < 1.f);
}


// ─────────────────────────────────────────────────────────────────────────────
// 9. Jump while moving: horizontal velocity preserved through jump arc
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("jump: horizontal velocity maintained during airborne phase", "[movement][jump][gravity]")
{
	proto_client::Project project;
	TestScene scene;
	TestNetClient net;

	TriangleCollidable floor;
	AddFloor(floor);
	scene.SetCollidables({ &floor });

	const Vector3 startPos(0.f, 0.f, 0.f);
	auto unit = MakeUnit(scene, net, project, startPos);

	// Get to max speed first (60 ticks ≈ 1 s)
	SimulateMovement(*unit, Vector3(0.f, 0.f, 1.f), 60, kDt);

	const float vzBeforeJump = unit->GetUnitMovement()->GetVelocity().z;
	unit->Jump();

	// Simulate the jump arc while still providing forward input
	UnitMovement* mv = unit->GetUnitMovement();
	bool wasAirborne = false;
	float minVzDuringAir = vzBeforeJump;

	for (int i = 0; i < 120; ++i)
	{
		unit->AddInputVector(Vector3(0.f, 0.f, 1.f));
		mv->Tick(kDt);

		const Vector3 p = unit->GetPosition();
		const Vector3 v = mv->GetVelocity();
		if (p.y > 0.1f)
		{
			wasAirborne = true;
			minVzDuringAir = std::min(minVzDuringAir, v.z);
		}
	}

	const Vector3 finalPos = unit->GetPosition();
	const Vector3 finalVel = mv->GetVelocity();

	INFO("vzBeforeJump=" << vzBeforeJump << "  minVzDuringAir=" << minVzDuringAir);
	INFO("finalPos.z=" << finalPos.z << "  finalVel.z=" << finalVel.z);

	CHECK(!std::isnan(finalPos.z));
	// Unit must have become airborne
	CHECK(wasAirborne);
	// Z speed must not have dropped below zero during the arc
	CHECK(minVzDuringAir > 0.f);
	// After landing, Z speed should be near run speed again
	CHECK(finalVel.z == Approx(kRunSpeed).epsilon(0.15f));
	// Unit must have moved forward during the jump
	CHECK(finalPos.z > 10.f);
}


// ─────────────────────────────────────────────────────────────────────────────
// 10. Ramp walk: unit ascends a climbable slope without losing speed
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("ramp: unit climbs shallow walkable ramp and gains height", "[movement][ramp]")
{
	proto_client::Project project;
	TestScene scene;
	TestNetClient net;

	// Flat approach floor
	TriangleCollidable floor;
	AddFloor(floor);

	// Ramp: starts at Z=5, rises from Y=0 to Y=2 over 10 m (11.3° slope).
	// This is well within the walkable angle (cos(11.3°) ≈ 0.98 > walkableFloorY ~0.71).
	TriangleCollidable ramp;
	ramp.AddTriangle(
		Vector3(-5.f, 0.f,  5.f),
		Vector3(-5.f, 2.f, 15.f),
		Vector3( 5.f, 0.f,  5.f));
	ramp.AddTriangle(
		Vector3( 5.f, 0.f,  5.f),
		Vector3(-5.f, 2.f, 15.f),
		Vector3( 5.f, 2.f, 15.f));

	scene.SetCollidables({ &floor, &ramp });

	const Vector3 startPos(0.f, 0.f, 0.f);
	auto unit = MakeUnit(scene, net, project, startPos);

	// Walk forward (+Z) for 3 s — enough to fully climb the ramp
	SimulateMovement(*unit, Vector3(0.f, 0.f, 1.f), 180, kDt);

	const Vector3 pos = unit->GetPosition();
	const Vector3 vel = unit->GetUnitMovement()->GetVelocity();

	INFO("finalPos=(" << pos.x << "," << pos.y << "," << pos.z << ")");
	INFO("finalVel=(" << vel.x << "," << vel.y << "," << vel.z << ")");

	CHECK(!std::isnan(pos.y));
	// Unit must have climbed at least 1 m above start (partially or fully up ramp)
	CHECK(pos.y >= 1.f);
	// Z speed on ramp must not be catastrophically different from run speed
	CHECK(std::abs(vel.z) >= kRunSpeed * 0.3f);
}


// ─────────────────────────────────────────────────────────────────────────────
// 11. No position creep while idle: standing still keeps Y stable
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("idle: unit standing on floor does not drift or oscillate", "[movement][idle]")
{
	proto_client::Project project;
	TestScene scene;
	TestNetClient net;

	TriangleCollidable floor;
	AddFloor(floor);
	scene.SetCollidables({ &floor });

	const Vector3 startPos(0.f, 0.f, 0.f);
	auto unit = MakeUnit(scene, net, project, startPos);

	// Let the unit settle for 1 s with no input
	SimulateMovement(*unit, Vector3::Zero, 60, kDt);
	const Vector3 pos1 = unit->GetPosition();

	// Wait another 1 s
	SimulateMovement(*unit, Vector3::Zero, 60, kDt);
	const Vector3 pos2 = unit->GetPosition();

	INFO("pos after 1s=(" << pos1.x << "," << pos1.y << "," << pos1.z << ")");
	INFO("pos after 2s=(" << pos2.x << "," << pos2.y << "," << pos2.z << ")");

	const float drift = std::sqrt(
		(pos2.x-pos1.x)*(pos2.x-pos1.x) +
		(pos2.y-pos1.y)*(pos2.y-pos1.y) +
		(pos2.z-pos1.z)*(pos2.z-pos1.z));

	CHECK(!std::isnan(pos2.x));
	CHECK(!std::isnan(pos2.y));
	// Y must stay on the floor
	CHECK(pos2.y == Approx(0.f).margin(0.1f));
	// No horizontal drift with zero input
	CHECK(drift < 0.05f);
}


// ─────────────────────────────────────────────────────────────────────────────
// 12. Speed cap: acceleration never allows speed above runSpeed on flat ground
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("speed cap: velocity never exceeds run speed on open floor", "[movement][speed]")
{
	proto_client::Project project;
	TestScene scene;
	TestNetClient net;

	TriangleCollidable floor;
	AddFloor(floor);
	scene.SetCollidables({ &floor });

	auto unit = MakeUnit(scene, net, project, Vector3(0.f, 0.f, 0.f));

	float peakSpeed = 0.f;
	SimulateMovement(*unit, Vector3(0.f, 0.f, 1.f), 240, kDt,
		[&](int, const Vector3&, const Vector3& v)
		{
			const float s = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
			peakSpeed = std::max(peakSpeed, s);
		});

	INFO("peakSpeed=" << peakSpeed << "  runSpeed=" << kRunSpeed);
	// Allow 1% float tolerance — speed must not exceed run speed
	CHECK(peakSpeed <= kRunSpeed * 1.01f);
}

} // namespace mmo
