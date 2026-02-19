// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/movement_info.h"
#include "math/vector3.h"
#include "math/radian.h"

#include <deque>

namespace mmo
{
	/// @brief A snapshot of a remote player's movement state received from the network.
	/// These snapshots are buffered and played back with a configurable delay
	/// to provide smooth movement display for remote players.
	struct RemoteMovementSnapshot
	{
		/// @brief The timestamp of this snapshot (already mapped to local client time).
		GameTime timestamp = 0;

		/// @brief The world-space position reported in the packet.
		Vector3 position;

		/// @brief The facing (yaw) orientation of the remote player.
		Radian facing;

		/// @brief Movement flags at the time of this snapshot.
		uint32 movementFlags = 0;

		/// @brief Jump velocity at the time of this snapshot (only valid when Falling flag is set).
		Vector3 jumpVelocity;

		/// @brief How long the unit has been falling at this snapshot.
		GameTime fallTime = 0;
	};

	/// @brief The interpolated state output from the remote movement queue.
	/// This represents the computed visual state of a remote player at a given playback time.
	struct RemoteMovementState
	{
		/// @brief The interpolated world-space position.
		Vector3 position;

		/// @brief The interpolated facing (yaw).
		Radian facing;

		/// @brief The movement flags from the active snapshot (used for animation).
		uint32 movementFlags = 0;

		/// @brief Whether the remote player is currently in a jump/fall state.
		bool isFalling = false;

		/// @brief The current velocity (used for jump arc rendering and animation).
		Vector3 velocity;

		/// @brief Whether this state was produced by extrapolation beyond the last known snapshot.
		bool isExtrapolating = false;
	};

	/// @brief Manages a buffered queue of movement snapshots for a remote player.
	/// Incoming network movement packets are enqueued as snapshots. During each
	/// frame update, the queue is sampled at (now - bufferDelay) to produce a
	/// smooth interpolated state that is applied to the remote unit's scene node.
	///
	/// Design goals:
	///  - Absorb network jitter through a configurable playback delay (default 500ms).
	///  - Provide smooth interpolation between known sync points.
	///  - Handle jump arcs using physics simulation rather than linear interpolation.
	///  - Gracefully extrapolate when packets are delayed or lost.
	class RemoteMovementQueue
	{
	public:
		/// @brief Creates a new RemoteMovementQueue.
		RemoteMovementQueue();

		/// @brief Sets the playback buffer delay in milliseconds.
		/// @param delayMs Delay in milliseconds (e.g. 500 = half second buffer).
		void SetBufferDelay(GameTime delayMs);

		/// @brief Gets the current playback buffer delay in milliseconds.
		/// @return Buffer delay in milliseconds.
		[[nodiscard]] GameTime GetBufferDelay() const { return m_bufferDelay; }

		/// @brief Enqueues a new movement snapshot from a received network packet.
		/// Snapshots are expected to arrive roughly in chronological order.
		/// @param info The MovementInfo from the network packet.
		void EnqueueSnapshot(const MovementInfo& info);

		/// @brief Samples the queue at the given time to produce an interpolated state
		/// for rendering. This also prunes old snapshots that are no longer needed.
		/// @param now The current local client time in milliseconds.
		/// @param runSpeed The run speed of the remote player (for extrapolation).
		/// @param backwardsSpeed The backwards speed of the remote player.
		/// @param turnSpeed The turn speed of the remote player (rad/s).
		/// @param outState The interpolated movement state to apply.
		/// @return True if a valid state was produced, false if no snapshots exist yet.
		bool Sample(GameTime now, float runSpeed, float backwardsSpeed, float turnSpeed, RemoteMovementState& outState);

		/// @brief Returns whether the queue has been initialized with at least one snapshot.
		[[nodiscard]] bool IsInitialized() const { return m_initialized; }

		/// @brief Clears all buffered snapshots and resets state.
		void Clear();

		/// @brief Returns the number of currently buffered snapshots.
		[[nodiscard]] size_t GetSnapshotCount() const { return m_snapshots.size(); }

	private:
		/// @brief Extrapolates position from a snapshot using its movement flags.
		/// @param snapshot The snapshot to extrapolate from.
		/// @param elapsed Elapsed time in seconds since the snapshot.
		/// @param runSpeed Run speed for forward/strafe movement.
		/// @param backwardsSpeed Backwards movement speed.
		/// @param outPosition The extrapolated position.
		/// @param outFacing The extrapolated facing (with turn applied).
		/// @param turnSpeed Turn speed in rad/s.
		void ExtrapolateFromSnapshot(const RemoteMovementSnapshot& snapshot, float elapsed,
		                             float runSpeed, float backwardsSpeed, float turnSpeed,
		                             Vector3& outPosition, Radian& outFacing) const;

		/// @brief Simulates a jump/fall arc from a snapshot.
		/// @param snapshot The snapshot where falling began (or current falling state).
		/// @param elapsed Elapsed time in seconds since the snapshot.
		/// @param outPosition The simulated position (vertical component applied).
		/// @param outVelocity The simulated velocity at the given elapsed time.
		void SimulateJumpArc(const RemoteMovementSnapshot& snapshot, float elapsed,
		                     Vector3& outPosition, Vector3& outVelocity) const;

		/// @brief Simulates a curved movement path from a snapshot using facing interpolation.
		/// Uses small time steps to integrate position along an arc when turning while moving.
		/// @param snapshot The starting snapshot.
		/// @param elapsed Elapsed time in seconds since the snapshot.
		/// @param totalDuration Total duration of the interpolation interval in seconds.
		/// @param runSpeed Run speed for forward/strafe movement.
		/// @param backwardsSpeed Backwards movement speed.
		/// @param turnSpeed Turn speed in rad/s.
		/// @param totalFacingDelta Total facing change over the interval (radians, wrapped).
		/// @param outPosition The simulated position.
		/// @param outFacing The simulated facing at the given elapsed time.
		void SimulateMovementArc(const RemoteMovementSnapshot& snapshot, float elapsed, float totalDuration,
		                         float runSpeed, float backwardsSpeed, float turnSpeed,
		                         float totalFacingDelta,
		                         Vector3& outPosition, Radian& outFacing) const;

		/// @brief Prunes snapshots that are too old to be useful for interpolation.
		/// Keeps at least the most recent snapshot before playback time.
		/// @param playbackTime The current playback time.
		void PruneOldSnapshots(GameTime playbackTime);

	private:
		/// @brief Buffered snapshots sorted by timestamp.
		std::deque<RemoteMovementSnapshot> m_snapshots;

		/// @brief Playback buffer delay in milliseconds.
		GameTime m_bufferDelay = 500;

		/// @brief Whether at least one snapshot has been received.
		bool m_initialized = false;

		/// @brief Gravity scale used for jump simulation (matches UnitMovement).
		float m_gravityScale = 2.0f;

		/// @brief Maximum number of snapshots to keep in the buffer.
		static constexpr size_t MaxSnapshots = 60;
	};
}
