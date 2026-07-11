// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_client/remote_movement_renderer.h"
#include "game/movement_info.h"
#include "math/vector3.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace mmo;

namespace
{
	MovementInfo MakeStandingSnapshot(const Vector3& position)
	{
		MovementInfo info;
		info.timestamp     = 1000;
		info.position      = position;
		info.facing        = Radian(0.0f);
		info.movementFlags = movement_flags::None;
		return info;
	}

	constexpr float kRunSpeed  = 7.0f;
	constexpr float kBackSpeed = 4.5f;
	constexpr float kWalkSpeed = 2.5f;

	/// Mirrors the per-frame flow of GameUnitC::UpdateRemoteMovement for a
	/// grounded, non-moving unit: sample the renderer, place the node, apply
	/// UnitMovement::CorrectGroundHeight (which snaps to the ground when the
	/// node is at least 1cm away from it) and feed the result back via
	/// SetRenderedY. Returns the rendered (post-snap) node Y for this frame.
	float StepIdleFrame(RemoteMovementRenderer& renderer, const float deltaTime, const float groundY)
	{
		RemoteMovementState state;
		REQUIRE(renderer.Sample(deltaTime, kRunSpeed, kBackSpeed, kWalkSpeed, 3.14f, state));

		float nodeY = state.position.y;
		if (std::abs(nodeY - groundY) >= 0.01f)
		{
			nodeY = groundY;
		}

		renderer.SetRenderedY(nodeY);
		return nodeY;
	}
}

// ---------------------------------------------------------------------------
// A remote player standing still whose last authoritative Y differs slightly
// from the locally resolved ground height (server navmesh vs. client collision
// mesh mismatch) must come to rest. The correction loop must not keep pulling
// the scene node toward the stale server Y against the ground snap — that
// produces a permanent millimetre sawtooth ("vibrating" characters).
// ---------------------------------------------------------------------------
TEST_CASE("RemoteMovementRenderer: stationary unit with server/ground Y mismatch does not oscillate", "[remote_movement_renderer][ground_snap]")
{
	RemoteMovementRenderer renderer;

	const float groundY = 10.17f;  // locally resolved ground height (incl. ground offset)
	const float serverY = 10.22f;  // authoritative Y from the last packet, 5cm above

	renderer.OnAuthoritativeUpdate(MakeStandingSnapshot(Vector3(3.0f, serverY, -2.0f)), false);

	const float dt = 1.0f / 60.0f;

	// Warm-up: two simulated seconds, far beyond the correction half-life.
	for (int i = 0; i < 120; ++i)
	{
		StepIdleFrame(renderer, dt, groundY);
	}

	// Observe one more second: the rendered Y must be visually stable.
	float minY = std::numeric_limits<float>::max();
	float maxY = std::numeric_limits<float>::lowest();
	for (int i = 0; i < 60; ++i)
	{
		const float y = StepIdleFrame(renderer, dt, groundY);
		minY = std::min(minY, y);
		maxY = std::max(maxY, y);
	}

	CHECK(maxY - minY < 0.002f);

	// And it must rest on the locally resolved ground, not hover at server Y.
	CHECK(maxY == Approx(groundY).margin(0.011f));
}

// ---------------------------------------------------------------------------
// Same scenario at a higher frame rate: smaller per-frame drift used to hover
// just below the 1cm snap threshold for several frames before snapping back,
// making the sawtooth frame-rate dependent. Must be stable at 144 fps too.
// ---------------------------------------------------------------------------
TEST_CASE("RemoteMovementRenderer: stationary unit is stable at high frame rates", "[remote_movement_renderer][ground_snap]")
{
	RemoteMovementRenderer renderer;

	const float groundY = 4.0f;
	const float serverY = 3.88f;  // server Y 12cm below local ground

	renderer.OnAuthoritativeUpdate(MakeStandingSnapshot(Vector3(-1.0f, serverY, 5.0f)), false);

	const float dt = 1.0f / 144.0f;

	for (int i = 0; i < 288; ++i)
	{
		StepIdleFrame(renderer, dt, groundY);
	}

	float minY = std::numeric_limits<float>::max();
	float maxY = std::numeric_limits<float>::lowest();
	for (int i = 0; i < 144; ++i)
	{
		const float y = StepIdleFrame(renderer, dt, groundY);
		minY = std::min(minY, y);
		maxY = std::max(maxY, y);
	}

	CHECK(maxY - minY < 0.002f);
	CHECK(minY == Approx(groundY).margin(0.011f));
}

// ---------------------------------------------------------------------------
// A fresh authoritative update must still reset the dead-reckoning target so
// real position corrections keep working after ground snaps synced the Y.
// ---------------------------------------------------------------------------
TEST_CASE("RemoteMovementRenderer: authoritative update still corrects position after ground snaps", "[remote_movement_renderer][ground_snap]")
{
	RemoteMovementRenderer renderer;

	const float groundY = 10.17f;

	renderer.OnAuthoritativeUpdate(MakeStandingSnapshot(Vector3(0.0f, 10.22f, 0.0f)), false);

	const float dt = 1.0f / 60.0f;
	for (int i = 0; i < 120; ++i)
	{
		StepIdleFrame(renderer, dt, groundY);
	}

	// New authoritative position 2m away (e.g. the player actually moved).
	renderer.OnAuthoritativeUpdate(MakeStandingSnapshot(Vector3(2.0f, 10.22f, 0.0f)), false);

	// The correction loop must converge the scene position toward the new X.
	RemoteMovementState state;
	for (int i = 0; i < 120; ++i)
	{
		REQUIRE(renderer.Sample(dt, kRunSpeed, kBackSpeed, kWalkSpeed, 3.14f, state));
	}

	CHECK(state.position.x == Approx(2.0f).margin(0.05f));
}

// ---------------------------------------------------------------------------
// Walk mode: a remote player moving forward with the WalkMode flag set must be
// dead-reckoned at walk speed, not run speed. Extrapolating at run speed makes
// the prediction race ahead of the real (walking) player, so every heartbeat
// produces a visible pull-back correction.
// ---------------------------------------------------------------------------
TEST_CASE("RemoteMovementRenderer: walking player is dead-reckoned at walk speed", "[remote_movement_renderer][walk_mode]")
{
	RemoteMovementRenderer renderer;

	MovementInfo info;
	info.timestamp     = 1000;
	info.position      = Vector3::Zero;
	info.facing        = Radian(0.0f);  // forward = +X
	info.movementFlags = movement_flags::Forward | movement_flags::WalkMode;

	renderer.OnAuthoritativeUpdate(info, false);

	const float dt = 1.0f / 60.0f;
	RemoteMovementState state;
	Vector3 traveled = Vector3::Zero;
	for (int i = 0; i < 60; ++i)  // one simulated second
	{
		REQUIRE(renderer.Sample(dt, kRunSpeed, kBackSpeed, kWalkSpeed, 3.14f, state));
		traveled += state.desiredDelta;
	}

	// After 1s of walking forward, the dead-reckoned distance must match walk
	// speed (2.5 m), not run speed (7 m).
	CHECK(traveled.GetLength() == Approx(kWalkSpeed).margin(0.05f));
	CHECK(state.velocity.GetLength() == Approx(kWalkSpeed).margin(0.01f));
}

// ---------------------------------------------------------------------------
// Walking backwards keeps the dedicated backwards speed — the local player's
// input code applies movement_type::Backwards regardless of walk mode, and the
// remote prediction must mirror that exactly.
// ---------------------------------------------------------------------------
TEST_CASE("RemoteMovementRenderer: walking backwards still uses backwards speed", "[remote_movement_renderer][walk_mode]")
{
	RemoteMovementRenderer renderer;

	MovementInfo info;
	info.timestamp     = 1000;
	info.position      = Vector3::Zero;
	info.facing        = Radian(0.0f);
	info.movementFlags = movement_flags::Backward | movement_flags::WalkMode;

	renderer.OnAuthoritativeUpdate(info, false);

	const float dt = 1.0f / 60.0f;
	RemoteMovementState state;
	Vector3 traveled = Vector3::Zero;
	for (int i = 0; i < 60; ++i)
	{
		REQUIRE(renderer.Sample(dt, kRunSpeed, kBackSpeed, kWalkSpeed, 3.14f, state));
		traveled += state.desiredDelta;
	}

	CHECK(traveled.GetLength() == Approx(kBackSpeed).margin(0.05f));
}

// ---------------------------------------------------------------------------
// Without the WalkMode flag, forward movement keeps using run speed.
// ---------------------------------------------------------------------------
TEST_CASE("RemoteMovementRenderer: running player is dead-reckoned at run speed", "[remote_movement_renderer][walk_mode]")
{
	RemoteMovementRenderer renderer;

	MovementInfo info;
	info.timestamp     = 1000;
	info.position      = Vector3::Zero;
	info.facing        = Radian(0.0f);
	info.movementFlags = movement_flags::Forward;

	renderer.OnAuthoritativeUpdate(info, false);

	const float dt = 1.0f / 60.0f;
	RemoteMovementState state;
	Vector3 traveled = Vector3::Zero;
	for (int i = 0; i < 60; ++i)
	{
		REQUIRE(renderer.Sample(dt, kRunSpeed, kBackSpeed, kWalkSpeed, 3.14f, state));
		traveled += state.desiredDelta;
	}

	CHECK(traveled.GetLength() == Approx(kRunSpeed).margin(0.05f));
}
