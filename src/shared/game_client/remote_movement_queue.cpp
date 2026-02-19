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

	RemoteMovementQueue::RemoteMovementQueue()
		: m_bufferDelay(500)
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

		// Case 1: We have two snapshots to interpolate between
		if (next && next->timestamp > prev->timestamp && playbackTime >= prev->timestamp && playbackTime <= next->timestamp)
		{
			const float totalDuration = static_cast<float>(next->timestamp - prev->timestamp) / 1000.0f;
			const float elapsed = static_cast<float>(playbackTime - prev->timestamp) / 1000.0f;
			const float t = (totalDuration > 0.0f) ? std::min(1.0f, elapsed / totalDuration) : 1.0f;

			// Interpolate facing first (shortest arc) — used by both branches
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

			// Check if we're in a falling/jumping state
			const bool prevFalling = (prev->movementFlags & movement_flags::Falling) != 0;

			if (prevFalling)
			{
				// During a jump/fall, simulate the physics arc from the prev snapshot
				Vector3 simPos = prev->position;
				Vector3 simVel;
				SimulateJumpArc(*prev, elapsed, simPos, simVel);

				// For the lateral component, simulate curved path based on facing interpolation,
				// then blend toward the known next position to correct drift.
				Vector3 simLateralPos;
				Radian simFacing;
				SimulateMovementArc(*prev, elapsed, totalDuration, runSpeed, backwardsSpeed, turnSpeed,
				                    facingDiff, simLateralPos, simFacing);

				// Blend the simulated lateral position toward the known destination
				const Vector3 targetLateral(next->position.x, 0.0f, next->position.z);
				const Vector3 curLateral(simLateralPos.x, 0.0f, simLateralPos.z);
				// Use smooth blend that increases correction as we approach t=1
				const float blendFactor = t * t; // Quadratic ease-in for correction
				const Vector3 blendedLateral = curLateral + (targetLateral - curLateral) * blendFactor;

				outState.position = Vector3(blendedLateral.x, simPos.y, blendedLateral.z);
				outState.velocity = simVel;
				outState.isFalling = true;
			}
			else
			{
				// Walking/standing: simulate curved path using facing interpolation
				// and movement flags, then blend toward known next position.
				Vector3 simPos;
				Radian simFacing;
				SimulateMovementArc(*prev, elapsed, totalDuration, runSpeed, backwardsSpeed, turnSpeed,
				                    facingDiff, simPos, simFacing);

				// Blend toward the known next position to correct accumulated drift.
				// Use quadratic ease-in so corrections accelerate toward the end of the interval.
				const float blendFactor = t * t;
				outState.position = simPos + (next->position - simPos) * blendFactor;

				outState.velocity = (totalDuration > 0.0f)
					? (next->position - prev->position) / totalDuration
					: Vector3::Zero;
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
				ExtrapolateFromSnapshot(*prev, clampedElapsed, runSpeed, backwardsSpeed, turnSpeed, extraPos, extraFacing);

				// Use the Y from physics simulation, lateral from extrapolation
				outState.position = Vector3(extraPos.x, simPos.y, extraPos.z);
				outState.velocity = simVel;
				outState.facing = extraFacing;
				outState.isFalling = true;
			}
			else
			{
				// Extrapolate laterally based on movement flags
				ExtrapolateFromSnapshot(*prev, clampedElapsed, runSpeed, backwardsSpeed, turnSpeed,
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
		// Use the jump velocity from the snapshot
		Vector3 velocity = snapshot.jumpVelocity;
		const float gravity = -GRAVITY_ACCELERATION * m_gravityScale;

		// Integrate velocity with gravity over elapsed time
		// position = start + velocity * t + 0.5 * gravity * t^2
		// velocity = startVelocity + gravity * t
		outPosition.y = outPosition.y + velocity.y * elapsed + 0.5f * gravity * elapsed * elapsed;

		// Also apply lateral velocity from jump
		outPosition.x += velocity.x * elapsed;
		outPosition.z += velocity.z * elapsed;

		// Compute current velocity
		outVelocity = velocity;
		outVelocity.y += gravity * elapsed;
	}

	void RemoteMovementQueue::SimulateMovementArc(const RemoteMovementSnapshot& snapshot, const float elapsed,
	                                               const float totalDuration,
	                                               const float runSpeed, const float backwardsSpeed,
	                                               const float turnSpeed, const float totalFacingDelta,
	                                               Vector3& outPosition, Radian& outFacing) const
	{
		outPosition = snapshot.position;
		outFacing = snapshot.facing;

		if (elapsed <= 0.0f)
		{
			return;
		}

		// Determine if the player is turning (based on movement flags or facing delta)
		const bool isTurning = (snapshot.movementFlags & movement_flags::TurnLeft) != 0 ||
		                       (snapshot.movementFlags & movement_flags::TurnRight) != 0 ||
		                       std::abs(totalFacingDelta) > 0.01f;

		const bool isMoving = (snapshot.movementFlags & movement_flags::Moving) != 0;

		// If the player is not turning, or not moving, just use straight-line extrapolation
		if (!isTurning || !isMoving)
		{
			ExtrapolateFromSnapshot(snapshot, elapsed, runSpeed, backwardsSpeed, turnSpeed,
			                        outPosition, outFacing);
			return;
		}

		// Simulate curved movement using small time steps.
		// We integrate position using the continuously changing facing.
		constexpr int numSteps = 16;
		const float dt = elapsed / static_cast<float>(numSteps);
		const float startFacing = snapshot.facing.GetValueRadians();

		// Compute facing rate: use the known total facing delta if we have a valid interval,
		// otherwise fall back to turn speed from flags.
		float facingRate;
		if (totalDuration > 0.001f)
		{
			facingRate = totalFacingDelta / totalDuration;
		}
		else
		{
			facingRate = 0.0f;
			if (snapshot.movementFlags & movement_flags::TurnLeft)
			{
				facingRate = turnSpeed;
			}
			if (snapshot.movementFlags & movement_flags::TurnRight)
			{
				facingRate = -turnSpeed;
			}
		}

		Vector3 pos = snapshot.position;

		for (int step = 0; step < numSteps; ++step)
		{
			const float stepTime = (static_cast<float>(step) + 0.5f) * dt;
			const float currentFacing = startFacing + facingRate * stepTime;

			const Quaternion orientation(Radian(currentFacing), Vector3::UnitY);
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

			pos += moveDir * dt;
		}

		outPosition = pos;
		outFacing = Radian(startFacing + facingRate * elapsed);
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
