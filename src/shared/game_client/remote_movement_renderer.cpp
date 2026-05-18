// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "remote_movement_renderer.h"
#include "game/movement_info.h"
#include "math/quaternion.h"
#include "log/default_log_levels.h"
#include "base/utilities.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	RemoteMovementRenderer::RemoteMovementRenderer() = default;

	void RemoteMovementRenderer::OnAuthoritativeUpdate(const MovementInfo& info, const bool facingOnly)
	{
		if (facingOnly)
		{
			// Facing-only update (MoveSetFacing, MoveStopTurn):
			// Apply immediately — rotation has no momentum, no blending needed.
			m_authFacing     = info.facing;
			m_renderedFacing = info.facing;
			m_authFlags      = info.movementFlags;
			return;
		}

		if (!m_initialized)
		{
			// First update — seed all state from the packet.
			m_authPos            = info.position;
			m_authFacing         = info.facing;
			m_authFlags          = info.movementFlags;
			m_authTimestamp      = info.timestamp;
			m_renderedPos        = info.position;
			m_scenePos           = info.position;
			m_renderedFacing     = info.facing;

			if (info.IsFalling())
			{
				m_fallVelocity     = info.jumpVelocity;
				m_fallTimeAccumSec = static_cast<float>(info.fallTime) / 1000.0f;
				m_fallVelocity.y  += kGravity * m_fallTimeAccumSec;
			}
			else
			{
				m_fallVelocity     = Vector3::Zero;
				m_fallTimeAccumSec = 0.0f;
			}

			m_initialized = true;
			return;
		}

		const float corrLen = (info.position - m_renderedPos).GetLength();
		DLOG("[RemoteMovement] auth update corr=" << corrLen
		    << "m flags=" << log_hex_digit(info.movementFlags));

		// Reset dead-reckoning to the authoritative position.
		// m_scenePos (scene node) is NOT snapped here — the proportional correction
		// loop in Sample() will smooth it toward m_renderedPos over the coming frames.
		// Exception: very large corrections (teleports) snap both immediately.
		if (corrLen >= kTeleportThreshold)
		{
			m_scenePos = info.position;
		}

		m_authPos        = info.position;
		m_authFacing     = info.facing;
		m_authFlags      = info.movementFlags;
		m_authTimestamp  = info.timestamp;
		m_renderedFacing = info.facing;
		m_renderedPos    = info.position;

		// Reset fall state on any full authoritative update.
		if (info.IsFalling())
		{
			m_fallVelocity     = info.jumpVelocity;
			m_fallTimeAccumSec = static_cast<float>(info.fallTime) / 1000.0f;
			m_fallVelocity.y  += kGravity * m_fallTimeAccumSec;
		}
		else
		{
			m_fallVelocity     = Vector3::Zero;
			m_fallTimeAccumSec = 0.0f;
		}
	}

	bool RemoteMovementRenderer::Sample(const float deltaTime, const float runSpeed,
	                                    const float backwardsSpeed, const float turnSpeed,
	                                    RemoteMovementState& outState)
	{
		if (!m_initialized)
		{
			return false;
		}

		const bool isFalling = (m_authFlags & movement_flags::Falling) != 0;

		// ── 1. Apply turn ─────────────────────────────────────────────────────
		if (m_authFlags & movement_flags::TurnLeft)
		{
			m_renderedFacing = Radian(m_renderedFacing.GetValueRadians() + turnSpeed * deltaTime);
		}
		if (m_authFlags & movement_flags::TurnRight)
		{
			m_renderedFacing = Radian(m_renderedFacing.GetValueRadians() - turnSpeed * deltaTime);
		}

		// ── 2. Dead-reckoning: advance m_renderedPos (the authoritative target) ─
		const Quaternion orientation(m_renderedFacing, Vector3::UnitY);
		const Vector3 forward = orientation * Vector3::UnitX;
		const Vector3 right   = orientation * Vector3::UnitZ;

		Vector3 desiredDelta = Vector3::Zero;

		if (isFalling)
		{
			const Vector3 lateral = ComputeVelocity(forward, right, m_authFlags, runSpeed, backwardsSpeed);
			desiredDelta.x = lateral.x * deltaTime;
			desiredDelta.z = lateral.z * deltaTime;
			StepFallArc(deltaTime);
			desiredDelta.y = m_fallVelocity.y * deltaTime;
			m_renderedPos += desiredDelta;
		}
		else
		{
			desiredDelta = ComputeVelocity(forward, right, m_authFlags, runSpeed, backwardsSpeed) * deltaTime;
			m_renderedPos += desiredDelta;
		}

		// ── 3. Smooth correction: pull m_scenePos toward m_renderedPos ────────
		// Rather than snapping the scene node to the dead-reckoned position on
		// every correction (which causes 10 Hz jitter), we apply a proportional
		// correction each frame.  This closes normal heartbeat-drift gaps (≤ 0.7 m)
		// in about 70 ms without visible snapping.
		//
		// The scene node will be placed at m_scenePos below; game_unit_c then runs
		// RemotePlayerMoveCollide to resolve collisions from that position, and calls
		// SetRenderedPos to write the post-collision result back into m_scenePos.
		// This keeps the scene node and the correction loop in sync.
		const Vector3 corrVec = m_renderedPos - m_scenePos;
		const float corrLen = corrVec.GetLength();
		if (corrLen > 0.001f)
		{
			const float maxStep = kCorrectionSpeed * deltaTime;
			if (corrLen <= maxStep)
			{
				// Close enough — snap the remaining gap this frame.
				m_scenePos = m_renderedPos;
			}
			else
			{
				m_scenePos += corrVec * (maxStep / corrLen);
			}
		}

		// ── 4. Fill output ────────────────────────────────────────────────────
		// position: where game_unit_c should place the scene node before the
		//   collision sweep.  This is m_scenePos (which has been nudged toward
		//   m_renderedPos by the correction above) minus the delta that will be
		//   applied this frame via RemotePlayerMoveCollide — so after the sweep
		//   the scene node ends up at approximately m_scenePos.
		// desiredDelta: the movement delta to pass to RemotePlayerMoveCollide.
		outState.position      = m_scenePos - desiredDelta;
		outState.desiredDelta  = desiredDelta;
		outState.facing        = m_renderedFacing;
		outState.movementFlags = m_authFlags;
		outState.isFalling     = isFalling;
		outState.velocity      = isFalling ? m_fallVelocity : ComputeVelocity(forward, right, m_authFlags, runSpeed, backwardsSpeed);

		return true;
	}

	void RemoteMovementRenderer::Reset()
	{
		m_initialized        = false;
		m_fallVelocity       = Vector3::Zero;
		m_fallTimeAccumSec   = 0.0f;
		m_authFlags          = 0;
	}

	Vector3 RemoteMovementRenderer::ComputeVelocity(const Vector3& forward, const Vector3& right,
	                                                  const uint32 flags, const float runSpeed,
	                                                  const float backwardsSpeed) const
	{
		Vector3 vel = Vector3::Zero;
		if (flags & movement_flags::Forward)     vel += forward * runSpeed;
		if (flags & movement_flags::Backward)    vel -= forward * backwardsSpeed;
		if (flags & movement_flags::StrafeLeft)  vel -= right   * runSpeed;
		if (flags & movement_flags::StrafeRight) vel += right   * runSpeed;
		return vel;
	}

	void RemoteMovementRenderer::StepFallArc(const float deltaTime)
	{
		m_fallVelocity.y   += kGravity * deltaTime;
		m_fallTimeAccumSec += deltaTime;
	}
}
