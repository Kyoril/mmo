// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Dead-reckoning remote movement renderer.
//
// Design:
//   1. Maintains the last authoritative state (position, facing, movement flags,
//      timestamp) received from the server.
//   2. Every frame, extrapolates position forward from that state using the unit's
//      movement flags and elapsed time — exactly what the local physics engine does
//      for the local player.  This gives us m_renderedPos (the "true" target).
//   3. A separate m_scenePos tracks where the scene node (and collision capsule)
//      actually is.  Each frame we compute an error vector (m_renderedPos − m_scenePos)
//      and apply a fraction of it — a smooth proportional correction — so the scene
//      node converges to the dead-reckoned target without snapping.
//   4. Facing updates (MoveSetFacing) are applied immediately as facing-only.
//      MoveStopTurn carries an authoritative position and resets dead-reckoning.
//
// With the heartbeat at 500ms the maximum dead-reckoning error is
//   run_speed (7 m/s) × 0.5 s = 3.5 m.
// kCorrectionSpeed (20 m/s) closes a 3.5 m gap in ~175 ms — tracks
// authoritative position closely without looking like a snap.

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
		Vector3 position;       ///< Visual position to apply to the scene node (includes correction)
		Vector3 desiredDelta;   ///< Raw lateral movement this frame (before collision resolution)
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

		/// @brief Returns the current dead-reckoned position (before correction offset).
		[[nodiscard]] const Vector3& GetRenderedPos() const { return m_renderedPos; }

		/// @brief Overrides the scene-node position after external ground/collision correction.
		/// Dead reckoning target is unaffected; only the scene-node tracking position changes.
		void SetRenderedPos(const Vector3& pos) { m_scenePos = pos; }

		/// @brief Overrides the scene-node Y after ground snap (e.g. terrain following).
		/// Dead reckoning target Y is unaffected.
		void SetRenderedY(const float y) { m_scenePos.y = y; }

		/// @brief Resets all state (e.g. on despawn/teleport).
		void Reset();

	private:
		/// @brief Computes the velocity vector for a given movement state.
		Vector3 ComputeVelocity(const Vector3& forward, const Vector3& right,
		                        uint32 flags, float runSpeed, float backwardsSpeed) const;

		/// @brief Simulates one step of jump/fall physics.
		void StepFallArc(float deltaTime);

	private:
		bool m_initialized = false;

		// ── Authoritative state (from last server packet) ──────────────────────
		Vector3  m_authPos;
		Radian   m_authFacing;
		uint32   m_authFlags = 0;
		GameTime m_authTimestamp = 0;

		// ── Dead-reckoned target position (advances every frame from m_authPos) ─
		// This is where physics says the player IS, based on last known inputs.
		// SetRenderedPos does NOT modify this — only OnAuthoritativeUpdate resets it.
		Vector3  m_renderedPos;
		Radian   m_renderedFacing;

		// ── Scene-node tracking position ───────────────────────────────────────
		// Tracks where the collision capsule/scene node actually is.
		// SetRenderedPos writes here.  Each frame a proportional correction nudges
		// m_scenePos toward m_renderedPos so the scene node smoothly converges to
		// the dead-reckoned truth without 10 Hz snap-back jitter.
		Vector3  m_scenePos;

		// ── Fall/jump state ────────────────────────────────────────────────────
		Vector3  m_fallVelocity;
		float    m_fallTimeAccumSec = 0.0f;

		// ── Tuning constants ───────────────────────────────────────────────────

		/// @brief Speed (m/s) at which m_scenePos is pulled toward m_renderedPos.
		/// At 10 m/s a 0.7 m correction (max 100ms heartbeat drift) closes in ~70 ms.
		static constexpr float kCorrectionSpeed = 20.0f;

		/// @brief Corrections larger than this (e.g. teleports) are snapped instantly
		/// rather than smoothed, so the player doesn't slide across the map.
		static constexpr float kTeleportThreshold = 8.0f;   // 8 m

		static constexpr float kGravity = -9.8f * 2.0f;  // matches UnitMovement gravity scale
	};
}
