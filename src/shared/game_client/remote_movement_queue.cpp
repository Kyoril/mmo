// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "remote_movement_queue.h"
#include "game/movement_info.h"
#include "math/vector3.h"
#include "math/quaternion.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	/// @brief Gravity constant used for jump arc simulation (m/s^2, positive = downward).
	static constexpr float GRAVITY_ACCELERATION = 9.8f;

	/// @brief Default interpolation buffer delay in milliseconds.
	/// Covers ~100ms one-way internet latency. Adaptive jitter tracking can
	/// raise this at runtime for high-latency connections.
	static constexpr GameTime kRemoteMovementBufferDelayMs = 100;

	/// @brief Minimum buffer delay in milliseconds, enforced even on localhost.
	/// 50ms ensures we nearly always have a next snapshot available for interpolation.
	static constexpr GameTime kMinBufferDelayMs = 50;

	/// @brief Maximum buffer delay cap.
	static constexpr GameTime kMaxBufferDelayMs = 400;

	RemoteMovementQueue::RemoteMovementQueue()
		: m_bufferDelay(kRemoteMovementBufferDelayMs)
		, m_initialized(false)
		, m_gravityScale(2.0f)
	{
	}

	void RemoteMovementQueue::SetBufferDelay(const GameTime delayMs)
	{
		m_bufferDelay = delayMs;
	}

	void RemoteMovementQueue::EnqueueSnapshot(const MovementInfo& info)
	{
		RemoteMovementSnapshot snapshot;
		snapshot.timestamp = info.timestamp;
		snapshot.position = info.position;
		snapshot.facing = info.facing;
		snapshot.movementFlags = info.movementFlags;
		snapshot.jumpVelocity = info.jumpVelocity;
		snapshot.fallTime = info.fallTime;

		// Adaptive jitter tracking: measure inter-arrival jitter and adjust the
		// buffer delay so we almost always have a next snapshot available.
		// Target: bufferDelay = measured_interval * 1.5, clamped to [kMin, kMax].
		// This makes the queue self-tune to near-zero on localhost and expand
		// automatically on high-latency connections.
		if (m_initialized && !m_snapshots.empty())
		{
			const GameTime prevTs = m_snapshots.back().timestamp;
			if (snapshot.timestamp > prevTs)
			{
				const GameTime interval = snapshot.timestamp - prevTs;
				// Exponential moving average of inter-arrival interval (α=0.25)
				m_avgIntervalMs = static_cast<GameTime>(
					m_avgIntervalMs * 3 / 4 + interval / 4);
				// Target = 1.5× average interval, clamped
				const GameTime target = std::max(kMinBufferDelayMs,
				    std::min(kMaxBufferDelayMs, m_avgIntervalMs * 3 / 2));
				// Smoothly move toward target (avoid sudden jumps)
				if (target > m_bufferDelay)
					m_bufferDelay = std::min(m_bufferDelay + 5, target);
				else if (target < m_bufferDelay)
					m_bufferDelay = std::max(m_bufferDelay - 2, target);
			}
		}

		// Insert in order. Normally packets arrive in order, but just in case:
		if (m_snapshots.empty() || snapshot.timestamp >= m_snapshots.back().timestamp)
		{
			m_snapshots.push_back(snapshot);
		}
		else
		{
			// Find insertion point (rare case: out-of-order packet)
			auto it = std::lower_bound(m_snapshots.begin(), m_snapshots.end(), snapshot.timestamp,
				[](const RemoteMovementSnapshot& s, GameTime t)
				{
					return s.timestamp < t;
				});
			m_snapshots.insert(it, snapshot);
		}

		// Limit buffer size
		while (m_snapshots.size() > MaxSnapshots)
		{
			m_snapshots.pop_front();
		}

		m_initialized = true;
	}

	bool RemoteMovementQueue::Sample(const GameTime now, const float runSpeed, const float backwardsSpeed,
	                                  const float turnSpeed, RemoteMovementState& outState)
	{
		if (!m_initialized || m_snapshots.empty())
		{
			return false;
		}

		const GameTime playbackTime = (now >= m_bufferDelay) ? (now - m_bufferDelay) : 0;

		// Prune old snapshots we no longer need
		PruneOldSnapshots(playbackTime);

		if (m_snapshots.empty())
		{
			return false;
		}

		// Find the two snapshots bracketing playbackTime:
		// prev: latest snapshot with timestamp <= playbackTime
		// next: earliest snapshot with timestamp > playbackTime
		const RemoteMovementSnapshot* prev = nullptr;
		const RemoteMovementSnapshot* next = nullptr;

		for (size_t i = 0; i < m_snapshots.size(); ++i)
		{
			if (m_snapshots[i].timestamp <= playbackTime)
			{
				prev = &m_snapshots[i];
				if (i + 1 < m_snapshots.size())
				{
					next = &m_snapshots[i + 1];
				}
			}
			else
			{
				// This snapshot is after playbackTime
				if (!prev && i == 0)
				{
					// Playback time is before our earliest snapshot - use the first one
					prev = &m_snapshots[0];
				}
				if (!next)
				{
					next = &m_snapshots[i];
				}
				break;
			}
		}

		// If no prev found (shouldn't happen after prune, but safety), use first
		if (!prev)
		{
			prev = &m_snapshots.front();
		}

		// Case 1: We have two snapshots to interpolate between.
		// Uses cubic Hermite spline interpolation to ensure velocity continuity
		// at snapshot boundaries, preventing the stutter/deceleration that occurs
		// with simple blended interpolation.
		if (next && next->timestamp > prev->timestamp && playbackTime >= prev->timestamp && playbackTime <= next->timestamp)
		{
			const float totalDuration = static_cast<float>(next->timestamp - prev->timestamp) / 1000.0f;
			const float elapsed = static_cast<float>(playbackTime - prev->timestamp) / 1000.0f;
			const float t = (totalDuration > 0.0f) ? std::min(1.0f, elapsed / totalDuration) : 1.0f;

			// Interpolate facing (shortest arc)
			float facingDiff = (next->facing - prev->facing).GetValueRadians();
			while (facingDiff > 3.14159265f)
			{
				facingDiff -= 6.28318530f;
			}
			while (facingDiff < -3.14159265f)
			{
				facingDiff += 6.28318530f;
			}
			outState.facing = Radian(prev->facing.GetValueRadians() + facingDiff * t);

			// Compute endpoint velocities from movement flags.
			// These serve as tangent vectors for Hermite interpolation.
			const Vector3 v0 = ComputeSnapshotVelocity(*prev, runSpeed, backwardsSpeed);
			const Vector3 v1 = ComputeSnapshotVelocity(*next, runSpeed, backwardsSpeed);

			// Cubic Hermite basis functions for parameter t in [0, 1]
			const float t2 = t * t;
			const float t3 = t2 * t;
			const float h00 =  2.0f * t3 - 3.0f * t2 + 1.0f;  // value at p0
			const float h10 =         t3 - 2.0f * t2 + t;       // tangent at p0
			const float h01 = -2.0f * t3 + 3.0f * t2;           // value at p1
			const float h11 =         t3 -        t2;            // tangent at p1

			// Tangent vectors scaled by interval duration.
			Vector3 m0 = v0 * totalDuration;
			Vector3 m1 = v1 * totalDuration;

			// Clamp tangent magnitudes to the chord length.
			// This prevents backward-motion artifacts when a character transitions
			// between stopped and moving: the positions are nearly identical but
			// the velocity tangent is large, causing the h11 basis function
			// (which is negative for t in 0..1) to pull the spline backwards.
			const float chordLength = (next->position - prev->position).GetLength();
			const float m0Len = m0.GetLength();
			const float m1Len = m1.GetLength();
			if (m0Len > chordLength && m0Len > 0.001f)
			{
				m0 = m0 * (chordLength / m0Len);
			}
			if (m1Len > chordLength && m1Len > 0.001f)
			{
				m1 = m1 * (chordLength / m1Len);
			}

			// For stop↔move transitions (one endpoint has zero velocity), the h11
			// basis term is negative in the middle of [0,1] and will pull the spline
			// backward even after chord clamping. Use linear interpolation for these
			// transitions — it's visually cleaner than a cubic that reverses.
			const bool v0Zero = v0.GetLength() < 0.001f;
			const bool v1Zero = v1.GetLength() < 0.001f;
			const bool useLinear = v0Zero || v1Zero;

			const bool prevFalling = (prev->movementFlags & movement_flags::Falling) != 0;

			if (prevFalling)
			{
				// Physics simulation for vertical (Y) axis
				Vector3 simPos = prev->position;
				Vector3 simVel;
				SimulateJumpArc(*prev, elapsed, simPos, simVel);

				// Lateral: linear for stop↔move, Hermite otherwise
				const Vector3 p0(prev->position.x, 0.0f, prev->position.z);
				const Vector3 p1(next->position.x, 0.0f, next->position.z);
				Vector3 lateralPos;
				if (useLinear)
				{
					lateralPos = p0 + (p1 - p0) * t;
				}
				else
				{
					const Vector3 lm0(m0.x, 0.0f, m0.z);
					const Vector3 lm1(m1.x, 0.0f, m1.z);
					lateralPos = p0 * h00 + lm0 * h10 + p1 * h01 + lm1 * h11;
				}

				outState.position = Vector3(lateralPos.x, simPos.y, lateralPos.z);
				outState.velocity = simVel;
				outState.isFalling = true;
			}
			else
			{
				if (useLinear)
				{
					// Linear interpolation for stop↔move transitions to avoid
					// Hermite backward-pull artifacts.
					outState.position = prev->position + (next->position - prev->position) * t;
				}
				else
				{
					// Full Hermite spline interpolation for steady-state movement.
					outState.position = prev->position * h00 + m0 * h10 + next->position * h01 + m1 * h11;
				}
				outState.velocity = v0 + (v1 - v0) * t;
				outState.isFalling = false;
			}

			outState.movementFlags = prev->movementFlags;
			outState.isExtrapolating = false;
		}
		// Case 2: We only have the prev snapshot, extrapolate from it
		else
		{
			// Guard against playbackTime being before prev (can happen on first few frames)
			const float rawElapsed = (playbackTime >= prev->timestamp)
				? static_cast<float>(playbackTime - prev->timestamp) / 1000.0f
				: 0.0f;
			const float clampedElapsed = std::min(rawElapsed, 2.0f); // Don't extrapolate more than 2 seconds

			// Compute a "stuck ratio" to suppress extrapolation when the character
			// is against a wall or otherwise not actually moving despite having movement
			// flags set. We compare the actual displacement between the last two
			// snapshots against what was predicted from movement flags. If the
			// character barely moved, we scale down extrapolation proportionally.
			float stuckRatio = 1.0f;
			if (m_snapshots.size() >= 2)
			{
				const auto& secondLast = m_snapshots[m_snapshots.size() - 2];
				const auto& last = m_snapshots[m_snapshots.size() - 1];
				const float dt = static_cast<float>(last.timestamp - secondLast.timestamp) / 1000.0f;
				if (dt > 0.01f)
				{
					const Vector3 predicted = ComputeSnapshotVelocity(secondLast, runSpeed, backwardsSpeed);
					const float predictedDist = predicted.GetLength() * dt;
					const float actualDist = (last.position - secondLast.position).GetLength();
					if (predictedDist > 0.1f)
					{
						stuckRatio = std::min(1.0f, actualDist / predictedDist);
					}
				}
			}

			const bool isFalling = (prev->movementFlags & movement_flags::Falling) != 0;

			if (isFalling)
			{
				// Simulate jump arc from the snapshot
				Vector3 simPos = prev->position;
				Vector3 simVel;
				SimulateJumpArc(*prev, clampedElapsed, simPos, simVel);

				// Also apply lateral extrapolation for the falling case
				Vector3 extraPos;
				Radian extraFacing;
				ExtrapolateFromSnapshot(*prev, clampedElapsed * stuckRatio, runSpeed, backwardsSpeed, turnSpeed, extraPos, extraFacing);

				// Use the Y from physics simulation, lateral from extrapolation
				outState.position = Vector3(extraPos.x, simPos.y, extraPos.z);
				outState.velocity = simVel;
				outState.facing = extraFacing;
				outState.isFalling = true;
			}
			else
			{
				// Extrapolate laterally based on movement flags, scaled by stuck ratio
				ExtrapolateFromSnapshot(*prev, clampedElapsed * stuckRatio, runSpeed, backwardsSpeed, turnSpeed,
				                        outState.position, outState.facing);
				outState.velocity = Vector3::Zero;
				outState.isFalling = false;
			}

			outState.movementFlags = prev->movementFlags;
			outState.isExtrapolating = (rawElapsed > 0.01f);
		}

		return true;
	}

	void RemoteMovementQueue::Clear()
	{
		m_snapshots.clear();
		m_initialized = false;
		m_avgIntervalMs = kRemoteMovementBufferDelayMs;
		m_bufferDelay = kRemoteMovementBufferDelayMs;
	}

	void RemoteMovementQueue::ExtrapolateFromSnapshot(const RemoteMovementSnapshot& snapshot, const float elapsed,
	                                                   const float runSpeed, const float backwardsSpeed,
	                                                   const float turnSpeed,
	                                                   Vector3& outPosition, Radian& outFacing) const
	{
		outPosition = snapshot.position;
		outFacing = snapshot.facing;

		if (elapsed <= 0.0f)
		{
			return;
		}

		// Apply turn
		if (snapshot.movementFlags & movement_flags::TurnLeft)
		{
			outFacing = Radian(outFacing.GetValueRadians() + turnSpeed * elapsed);
		}
		if (snapshot.movementFlags & movement_flags::TurnRight)
		{
			outFacing = Radian(outFacing.GetValueRadians() - turnSpeed * elapsed);
		}

		// Compute direction from the (possibly turned) facing  
		const Quaternion orientation(outFacing, Vector3::UnitY);
		const Vector3 forward = orientation * Vector3::UnitX;
		const Vector3 right = orientation * Vector3::UnitZ;

		Vector3 moveDir = Vector3::Zero;

		if (snapshot.movementFlags & movement_flags::Forward)
		{
			moveDir += forward * runSpeed;
		}
		if (snapshot.movementFlags & movement_flags::Backward)
		{
			moveDir -= forward * backwardsSpeed;
		}
		if (snapshot.movementFlags & movement_flags::StrafeLeft)
		{
			moveDir -= right * runSpeed;
		}
		if (snapshot.movementFlags & movement_flags::StrafeRight)
		{
			moveDir += right * runSpeed;
		}

		outPosition += moveDir * elapsed;
	}

	void RemoteMovementQueue::SimulateJumpArc(const RemoteMovementSnapshot& snapshot, const float elapsed,
	                                           Vector3& outPosition, Vector3& outVelocity) const
	{
		// jumpVelocity is the initial velocity at the START of the jump.
		// If this snapshot is mid-jump (e.g. a SetFacing packet while airborne),
		// fallTime tells us how long gravity has already been applied. We must
		// compute the current velocity at the snapshot moment before integrating
		// forward, otherwise we'd apply the full initial upward velocity from an
		// already-elevated position, causing massive Y overshoot / teleportation.
		const float gravity = -GRAVITY_ACCELERATION * m_gravityScale;
		const float alreadyFallen = static_cast<float>(snapshot.fallTime) / 1000.0f;

		Vector3 velocity = snapshot.jumpVelocity;
		velocity.y += gravity * alreadyFallen; // Current Y velocity at snapshot time

		// Integrate velocity with gravity over elapsed time from this snapshot:
		// position = start + currentVelocity * t + 0.5 * gravity * t^2
		// velocity = currentVelocity + gravity * t
		outPosition.y = outPosition.y + velocity.y * elapsed + 0.5f * gravity * elapsed * elapsed;

		// Lateral velocity from jump (not affected by gravity)
		outPosition.x += velocity.x * elapsed;
		outPosition.z += velocity.z * elapsed;

		// Compute velocity at the end of the integration period
		outVelocity = velocity;
		outVelocity.y += gravity * elapsed;
	}

	Vector3 RemoteMovementQueue::ComputeSnapshotVelocity(const RemoteMovementSnapshot& snapshot,
	                                                       const float runSpeed,
	                                                       const float backwardsSpeed) const
	{
		const Quaternion orientation(snapshot.facing, Vector3::UnitY);
		const Vector3 forward = orientation * Vector3::UnitX;
		const Vector3 right = orientation * Vector3::UnitZ;

		Vector3 vel = Vector3::Zero;

		if (snapshot.movementFlags & movement_flags::Forward)
		{
			vel += forward * runSpeed;
		}
		if (snapshot.movementFlags & movement_flags::Backward)
		{
			vel -= forward * backwardsSpeed;
		}
		if (snapshot.movementFlags & movement_flags::StrafeLeft)
		{
			vel -= right * runSpeed;
		}
		if (snapshot.movementFlags & movement_flags::StrafeRight)
		{
			vel += right * runSpeed;
		}

		return vel;
	}

	void RemoteMovementQueue::PruneOldSnapshots(const GameTime playbackTime)
	{
		// Keep at least the most recent snapshot before playback time.
		// Remove all snapshots before that except the one right before playbackTime.
		while (m_snapshots.size() > 2)
		{
			// Check if the second snapshot is still before playback time
			if (m_snapshots[1].timestamp <= playbackTime)
			{
				m_snapshots.pop_front();
			}
			else
			{
				break;
			}
		}
	}
}
