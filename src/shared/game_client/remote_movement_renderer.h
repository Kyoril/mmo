// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Dead-reckoning remote movement renderer.
//
// Design:
//   Instead of buffering packets and interpolating between them (which requires
//   a buffer delay and produces Hermite spline artifacts), this renderer:
//
//   1. Maintains the last authoritative state (position, facing, movement flags,
//      timestamp) received from the server.
//   2. Every frame, extrapolates position forward from that state using the unit's
//      movement flags and elapsed time — exactly what the local physics engine does
//      for the local player.
//   3. When a new authoritative update arrives (via OnAuthoritativeUpdate), it
//      records the correction vector (authoritative pos - current rendered pos) and
//      blends it away over kCorrectionBlendMs milliseconds using a linear fade.
//      This eliminates teleport artifacts while converging to ground truth quickly.
//   4. Facing updates (MoveSetFacing, MoveStopTurn) are applied immediately —
//      rotation has no momentum to predict and requires no blending.
//
// With the heartbeat at 100ms (S01), the maximum dead-reckoning error before a
// correction arrives is:  run_speed (7 m/s) * 0.1s = 0.7m.
// The 80ms blend window keeps corrections imperceptible at normal play speeds.

#pragma once

#include "base/typedefs.h"
#include "game/movement_info.h"
#include "math/vector3.h"
#include "math/radian.h"

namespace mmo
{
	/// @brief Output state produced by the renderer each frame.
	struct RemoteMovementState
	{
		Vector3 position;
		Radian  facing;
		uint32  movementFlags = 0;
		bool    isFalling = false;
		Vector3 velocity;
	};

	/// @brief Dead-reckoning renderer for remote player movement.
	///
	/// Thread safety: NOT thread-safe. Must only be called from the render thread.
	class RemoteMovementRenderer
	{
	public:
		RemoteMovementRenderer();

		/// @brief Called when an authoritative position+flags update arrives.
		/// @param info The MovementInfo from the network packet.
		/// @param facingOnly If true, only the facing is updated (no position extrapolation reset).
		void OnAuthoritativeUpdate(const MovementInfo& info, bool facingOnly);

		/// @brief Advance the renderer by deltaTime seconds and return the current visual state.
		/// @param deltaTime Frame delta in seconds.
		/// @param runSpeed Run/strafe speed in m/s.
		/// @param backwardsSpeed Backwards movement speed in m/s.
		/// @param turnSpeed Turn rate in rad/s.
		/// @param outState The visual state to apply to the scene node.
		/// @return True if initialized (at least one authoritative update received).
		bool Sample(float deltaTime, float runSpeed, float backwardsSpeed, float turnSpeed,
		            RemoteMovementState& outState);

		/// @brief Returns true if at least one authoritative update has been received.
		[[nodiscard]] bool IsInitialized() const { return m_initialized; }

		/// @brief Resets all state (e.g. on despawn/teleport).
		void Reset();

	private:
		/// @brief Computes the velocity vector for a given movement state.
		Vector3 ComputeVelocity(const Vector3& forward, const Vector3& right,
		                        uint32 flags, float runSpeed, float backwardsSpeed) const;

		/// @brief Simulates one step of jump/fall physics.
		void StepFallArc(float deltaTime);

	private:
		/// @brief Whether at least one authoritative update has been received.
		bool m_initialized = false;

		// ── Authoritative state (from last server packet) ──────────────────────
		Vector3  m_authPos;
		Radian   m_authFacing;
		uint32   m_authFlags = 0;
		GameTime m_authTimestamp = 0;

		// ── Rendered state (what the scene node currently shows) ───────────────
		Vector3  m_renderedPos;
		Radian   m_renderedFacing;

		// ── Blend correction ───────────────────────────────────────────────────
		/// @brief Start position of the current correction blend.
		Vector3  m_blendStartPos;
		/// @brief Target position of the current correction blend (= current dead-reckoned pos).
		Vector3  m_blendTargetPos;
		/// @brief Remaining blend time in seconds (0 = no active blend).
		float    m_blendTimeLeft = 0.0f;
		/// @brief Total duration of the current blend (for computing t).
		float    m_blendDuration = 0.0f;

		/// @brief Duration over which a correction is blended (seconds).
		static constexpr float kCorrectionBlendSec = 0.08f;   // 80ms

		// ── Fall/jump state ────────────────────────────────────────────────────
		Vector3  m_fallVelocity;
		float    m_fallTimeAccumSec = 0.0f;
		static constexpr float kGravity = -9.8f * 2.0f;  // matches UnitMovement gravity scale
	};
}
