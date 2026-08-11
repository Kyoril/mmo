// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_client/remote_movement_queue.h"
#include "game/movement_info.h"
#include "math/vector3.h"

using namespace mmo;

// ---------------------------------------------------------------------------
// Helper: Build a minimal MovementInfo with just the fields Sample() needs
// ---------------------------------------------------------------------------
static MovementInfo MakeSnapshot(GameTime timestamp, Vector3 position,
                                  uint32 flags = movement_flags::None,
                                  Vector3 jumpVelocity = Vector3::Zero,
                                  GameTime fallTime = 0)
{
	MovementInfo info;
	info.timestamp     = timestamp;
	info.position      = position;
	info.facing        = Radian(0.0f);
	info.movementFlags = flags;
	info.jumpVelocity  = jumpVelocity;
	info.fallTime      = fallTime;
	return info;
}

// ---------------------------------------------------------------------------
// Test 1 – Hold before first data: empty queue, Sample() returns false
// ---------------------------------------------------------------------------
TEST_CASE("RemoteMovementQueue: empty queue returns false", "[remote_movement][empty]")
{
	RemoteMovementQueue q;
	RemoteMovementState state;

	// With nothing enqueued, Sample() must return false regardless of now.
	CHECK_FALSE(q.Sample(1000, 7.0f, 4.5f, 3.14f, state));
	CHECK_FALSE(q.IsInitialized());
	CHECK(q.GetSnapshotCount() == 0);
}

// ---------------------------------------------------------------------------
// Test 2 – Interpolation between two snapshots
// Enqueue A at T=0, B at T=200ms.
// Sample at now=250ms  →  playbackTime = 250 - 150 = 100ms.
// T=100ms is half-way between A(T=0) and B(T=200ms), so the output position
// must lie strictly between A and B along the X axis.
// ---------------------------------------------------------------------------
TEST_CASE("RemoteMovementQueue: interpolation between two snapshots", "[remote_movement][interpolation]")
{
	RemoteMovementQueue q;

	const Vector3 posA(0.0f, 0.0f, 0.0f);
	const Vector3 posB(10.0f, 0.0f, 0.0f);

	q.EnqueueSnapshot(MakeSnapshot(0,   posA));
	q.EnqueueSnapshot(MakeSnapshot(200, posB));

	CHECK(q.IsInitialized());
	CHECK(q.GetSnapshotCount() == 2);

	// now=250ms → playbackTime=100ms (exactly halfway through [0,200])
	RemoteMovementState state;
	bool ok = q.Sample(250, 0.0f, 0.0f, 0.0f, state);

	REQUIRE(ok);
	CHECK_FALSE(state.isExtrapolating);

	// Position X must be strictly between A and B (Hermite tangents are zero
	// since runSpeed=0, so result should be linear interpolation at 0.5).
	CHECK(state.position.x > posA.x);
	CHECK(state.position.x < posB.x);

	// Y and Z must stay at ground level
	CHECK(state.position.y == Approx(0.0f).margin(0.01f));
	CHECK(state.position.z == Approx(0.0f).margin(0.01f));
}

// ---------------------------------------------------------------------------
// Test 3 – Out-of-order packet insertion
// Enqueue B (T=200ms) then A (T=0ms). The queue must sort them so that
// interpolation at playbackTime=100ms still returns a value between A and B.
// ---------------------------------------------------------------------------
TEST_CASE("RemoteMovementQueue: out-of-order packets are sorted", "[remote_movement][ordering]")
{
	RemoteMovementQueue q;

	const Vector3 posA(0.0f, 0.0f, 0.0f);
	const Vector3 posB(10.0f, 0.0f, 0.0f);

	// Deliberately enqueue later snapshot first
	q.EnqueueSnapshot(MakeSnapshot(200, posB));
	q.EnqueueSnapshot(MakeSnapshot(0,   posA));

	// Both must be retained and in order
	REQUIRE(q.GetSnapshotCount() == 2);

	RemoteMovementState state;
	bool ok = q.Sample(250, 0.0f, 0.0f, 0.0f, state);

	REQUIRE(ok);

	// After sorting: playbackTime=100ms is between T=0 and T=200, so
	// we must get a position between A and B.
	CHECK(state.position.x > posA.x - 0.01f);
	CHECK(state.position.x < posB.x + 0.01f);
}

// ---------------------------------------------------------------------------
// Test 4 – Extrapolation timeout clamp
// Enqueue a single stationary snapshot at T=0. Advance well past the 2-second
// extrapolation window. The queue STILL returns true (it has data) but must
// not produce unbounded drift: the position must match the snapshot exactly
// because there are no movement flags, and isExtrapolating is true once
// rawElapsed > 0.
// ---------------------------------------------------------------------------
TEST_CASE("RemoteMovementQueue: extrapolation clamp – stationary snapshot does not drift", "[remote_movement][extrapolation]")
{
	RemoteMovementQueue q;

	const Vector3 startPos(5.0f, 0.0f, 5.0f);

	// Single snapshot with no movement flags at T=0
	q.EnqueueSnapshot(MakeSnapshot(0, startPos, movement_flags::None));

	// now = 10000ms → playbackTime = 9850ms (well past 2s extrapolation cap)
	RemoteMovementState state;
	bool ok = q.Sample(10000, 7.0f, 4.5f, 3.14f, state);

	REQUIRE(ok);

	// No movement flags → extrapolated velocity is zero → position unchanged
	CHECK(state.position.x == Approx(startPos.x).margin(0.01f));
	CHECK(state.position.y == Approx(startPos.y).margin(0.01f));
	CHECK(state.position.z == Approx(startPos.z).margin(0.01f));

	// Extrapolation flag must be set (rawElapsed >> 0.01s)
	CHECK(state.isExtrapolating);
}

// ---------------------------------------------------------------------------
// Test 5 – Extrapolation clamp with movement flags does not drift unboundedly
// A snapshot with Forward flag and runSpeed=7 would travel 7*t metres if
// unclamped. With the 2-second clamp the maximum drift is 14 metres.
// Sampling at now=60000ms (60 seconds) must not exceed that limit.
// ---------------------------------------------------------------------------
TEST_CASE("RemoteMovementQueue: extrapolation clamp – moving snapshot drift bounded", "[remote_movement][extrapolation]")
{
	RemoteMovementQueue q;

	const Vector3 startPos(0.0f, 0.0f, 0.0f);
	const float runSpeed = 7.0f;

	// Snapshot at T=0 with Forward movement flag, facing along +X
	q.EnqueueSnapshot(MakeSnapshot(0, startPos, movement_flags::Forward));

	// Sample 60 seconds later; only 2 seconds of extrapolation allowed
	RemoteMovementState state;
	bool ok = q.Sample(60000, runSpeed, 4.5f, 0.0f, state);

	REQUIRE(ok);
	CHECK(state.isExtrapolating);

	// Maximum drift: runSpeed * 2.0s = 14m. Add a small margin.
	const float drift = (state.position - startPos).GetLength();
	CHECK(drift <= runSpeed * 2.0f + 0.1f);
}

// ---------------------------------------------------------------------------
// Test 6 – Jump-arc simulation
// Enqueue a snapshot at T=0 with Falling flag and upward jumpVelocity.Y.
// Gravity (9.8 * 2.0 = 19.6 m/s²) must pull position.Y down over time.
// Sample 300ms later (playbackTime = 300ms - 150ms = 150ms after snapshot).
// Initial Y velocity = +10 m/s, gravity = -19.6 m/s².
// Y after 0.15s: 10*0.15 + 0.5*(-19.6)*0.15^2 = 1.5 - 0.2205 ≈ 1.28m above start.
// So at 150ms position.Y > startPos.Y (still rising).
// ---------------------------------------------------------------------------
TEST_CASE("RemoteMovementQueue: jump-arc – position rises initially then falls under gravity", "[remote_movement][jump]")
{
	RemoteMovementQueue q;

	const Vector3 startPos(0.0f, 10.0f, 0.0f);
	Vector3 jumpVel(0.0f, 10.0f, 0.0f);  // 10 m/s upward

	q.EnqueueSnapshot(MakeSnapshot(0, startPos, movement_flags::Falling, jumpVel, 0));

	// Sample at now=300ms → playbackTime=150ms
	// Expected Y ≈ 10 + 10*0.15 - 0.5*19.6*0.15^2 ≈ 11.28
	RemoteMovementState state;
	bool ok = q.Sample(300, 7.0f, 4.5f, 0.0f, state);

	REQUIRE(ok);
	CHECK(state.isFalling);

	// Position must be above start at 150ms (still on the upswing)
	CHECK(state.position.y > startPos.y);
}

TEST_CASE("RemoteMovementQueue: jump-arc – position is lower than start after sufficient time", "[remote_movement][jump]")
{
	RemoteMovementQueue q;

	const Vector3 startPos(0.0f, 10.0f, 0.0f);
	Vector3 jumpVel(0.0f, 10.0f, 0.0f);  // 10 m/s upward

	q.EnqueueSnapshot(MakeSnapshot(0, startPos, movement_flags::Falling, jumpVel, 0));

	// At t=2.0s (past apex at ~0.51s), Y should be well below start.
	// But with a single snapshot and buffer delay of 150ms, playbackTime must
	// be past the 2-second extrapolation clamp → use extrapolation path.
	// We set now such that playbackTime = 2000ms (now=2150ms).
	// Y at 2.0s: 10 + 10*2 - 0.5*19.6*4 = 10+20-39.2 = -9.2 (below start)
	RemoteMovementState state;
	bool ok = q.Sample(2150, 7.0f, 4.5f, 0.0f, state);

	REQUIRE(ok);
	CHECK(state.isFalling);

	// Y must be lower than the starting Y after 2 seconds of free-fall arc
	CHECK(state.position.y < startPos.y);
}

// ---------------------------------------------------------------------------
// Test 7 – Clear resets queue
// ---------------------------------------------------------------------------
TEST_CASE("RemoteMovementQueue: Clear resets state", "[remote_movement][clear]")
{
	RemoteMovementQueue q;

	q.EnqueueSnapshot(MakeSnapshot(0, Vector3(1.0f, 2.0f, 3.0f)));
	REQUIRE(q.IsInitialized());
	REQUIRE(q.GetSnapshotCount() == 1);

	q.Clear();

	CHECK_FALSE(q.IsInitialized());
	CHECK(q.GetSnapshotCount() == 0);

	RemoteMovementState state;
	CHECK_FALSE(q.Sample(1000, 7.0f, 4.5f, 3.14f, state));
}

// ---------------------------------------------------------------------------
// Test 8 – SetBufferDelay round-trips correctly
// ---------------------------------------------------------------------------
TEST_CASE("RemoteMovementQueue: SetBufferDelay is honoured", "[remote_movement][config]")
{
	RemoteMovementQueue q;

	q.SetBufferDelay(250);
	CHECK(q.GetBufferDelay() == 250);

	q.SetBufferDelay(0);
	CHECK(q.GetBufferDelay() == 0);
}
