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

		// ── 3. Smooth correction: exponentially decay m_scenePos toward m_renderedPos ─
		// Use a critically-damped exponential blend rather than constant-speed chase.
		// This means small drift errors (normal heartbeat arc mismatch) are absorbed
		// gradually and invisibly, while large errors still converge within ~200 ms.
		// Formula: scenePos = lerp(scenePos, renderedPos, 1 - exp(-k * dt))
		// k = ln(2) / halfLife  →  k ≈ 6.93 for halfLife = 100ms.
		// At k=6.93: a 3.5 m gap (500ms heartbeat worst-case) halves every 100ms.
		//   t=100ms → 1.75 m remaining, t=200ms → 0.875 m, t=300ms → 0.44 m.
		// Teleports (>= kTeleportThreshold) still snap instantly.
		const Vector3 corrVec = m_renderedPos - m_scenePos;
		const float corrLen = corrVec.GetLength();
		if (corrLen > 0.001f)
		{
			if (corrLen >= kTeleportThreshold)
			{
				m_scenePos = m_renderedPos;
			}
			else
			{
				// alpha = 1 - exp(-k * dt), clamped to [0,1].
				const float alpha = 1.0f - std::exp(-kCorrectionDecay * deltaTime);
				m_scenePos += corrVec * alpha;
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
		Vector3 dir = Vector3::Zero;
		if (flags & movement_flags::Forward)     dir += forward;
		if (flags & movement_flags::Backward)    dir -= forward;
		if (flags & movement_flags::StrafeLeft)  dir -= right;
		if (flags & movement_flags::StrafeRight) dir += right;

		// Normalize so diagonal movement (forward + strafe) doesn't exceed run speed.
		// The local player's ScaleInputAcceleration clamps the input vector to length 1
		// before multiplying by MaxAcceleration, so forward+strafe travels at exactly
		// runSpeed, not runSpeed*sqrt(2).  Matching that here eliminates the systematic
		// overshoot that caused heartbeat corrections to visibly pull back strafing players.
		const float len = dir.GetLength();
		if (len > 0.001f)
		{
			dir /= len;
		}

		// Pure backward (no forward key): use the slower backwards speed.
		// Forward-only, strafe-only, or diagonal: use run speed.
		const float speed = ((flags & movement_flags::Backward) && !(flags & movement_flags::Forward))
			? backwardsSpeed
			: runSpeed;

		return dir * speed;
	}

	void RemoteMovementRenderer::StepFallArc(const float deltaTime)
	{
		m_fallVelocity.y   += kGravity * deltaTime;
		m_fallTimeAccumSec += deltaTime;
	}
}
