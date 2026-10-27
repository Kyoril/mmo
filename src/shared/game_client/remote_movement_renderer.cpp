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
	                                    const float backwardsSpeed, const float walkSpeed,
	                                    const float turnSpeed, RemoteMovementState& outState)
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
			const Vector3 lateral = ComputeVelocity(forward, right, m_authFlags, runSpeed, backwardsSpeed, walkSpeed);
			desiredDelta.x = lateral.x * deltaTime;
			desiredDelta.z = lateral.z * deltaTime;
			StepFallArc(deltaTime);
			desiredDelta.y = m_fallVelocity.y * deltaTime;
			m_renderedPos += desiredDelta;
		}
		else
		{
			desiredDelta = ComputeVelocity(forward, right, m_authFlags, runSpeed, backwardsSpeed, walkSpeed) * deltaTime;
			m_renderedPos += desiredDelta;
		}

		// ── 3. Smooth correction: exponentially decay m_scenePos toward m_renderedPos ─
		// A critically-damped exponential blend rather than a constant-speed chase: small
		// drift errors (normal heartbeat arc mismatch) are absorbed gradually and invisibly,
		// while large errors still converge within ~200 ms.
		// Formula: step = (renderedPos - scenePos) * (1 - exp(-k * dt)), k = ln(2) / halfLife.
		// At k=6.93: a 3.5 m gap (500ms heartbeat worst-case) halves every 100ms.
		// The target already includes this frame's dead-reckoned step, so the correction is the
		// whole move of the frame. While running it settles ~1 m behind the target, which lets a
		// stop packet (sent from slightly behind the prediction) end the run without a pull-back.
		// The step is not applied here: the caller sweeps it, so it can never pull the character
		// into geometry.
		Vector3 correction = Vector3::Zero;
		const Vector3 corrVec = m_renderedPos - m_scenePos;
		const float corrLen = corrVec.GetLength();

		m_stuckSec = (corrLen > kStuckDistance) ? m_stuckSec + deltaTime : 0.0f;

		if (corrLen >= kTeleportThreshold || m_stuckSec >= kStuckSnapSec)
		{
			// Teleports, and corrections the sweep could not complete, snap.
			m_scenePos = m_renderedPos;
			m_stuckSec = 0.0f;
		}
		else if (corrLen > 0.001f)
		{
			const float alpha = 1.0f - std::exp(-kCorrectionDecay * deltaTime);
			correction = corrVec * alpha;
		}

		// ── 4. Fill output ────────────────────────────────────────────────────
		// position: where the scene node currently is (the collision-resolved result of the
		//   previous frame, fed back via SetRenderedPos).
		// desiredDelta: this frame's move towards the target, which game_unit_c sweeps through
		//   RemotePlayerMoveCollide before feeding the resolved position back.
		outState.position      = m_scenePos;
		outState.desiredDelta  = correction;
		outState.facing        = m_renderedFacing;
		outState.movementFlags = m_authFlags;
		outState.isFalling     = isFalling;
		outState.velocity      = isFalling ? m_fallVelocity : ComputeVelocity(forward, right, m_authFlags, runSpeed, backwardsSpeed, walkSpeed);

		return true;
	}

	void RemoteMovementRenderer::ClipTargetToSurface(const Vector3& normal)
	{
		// Only a target that is being dead-reckoned can run on into an obstacle. A standing
		// player's target is its authoritative position and must stay exactly that: no further
		// packet arrives to restore it, and the sweep (or, failing that, the stuck snap) already
		// brings the character there around whatever is in the way.
		if ((m_authFlags & movement_flags::PositionChanging) == 0)
		{
			return;
		}

		const Vector3 toTarget = m_renderedPos - m_scenePos;
		const float intoSurface = toTarget.Dot(normal);
		if (intoSurface >= 0.0f)
		{
			return;
		}

		m_renderedPos -= normal * intoSurface;

		// Landed on something (a box top, a slope) before the prediction did: stop the predicted
		// fall there instead of accelerating downwards until the land packet arrives.
		if (normal.y > 0.7f && m_fallVelocity.y < 0.0f)
		{
			m_fallVelocity.y = 0.0f;
		}
	}

	void RemoteMovementRenderer::Reset()
	{
		m_initialized        = false;
		m_fallVelocity       = Vector3::Zero;
		m_fallTimeAccumSec   = 0.0f;
		m_stuckSec           = 0.0f;
		m_authFlags          = 0;
	}

	Vector3 RemoteMovementRenderer::ComputeVelocity(const Vector3& forward, const Vector3& right,
	                                                  const uint32 flags, const float runSpeed,
	                                                  const float backwardsSpeed, const float walkSpeed) const
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
		// Forward-only, strafe-only, or diagonal: use run speed, or walk speed
		// while walk mode is active. This mirrors the local player's input code,
		// which applies movement_type::Backwards regardless of walk mode —
		// extrapolating a walking player at run speed makes the prediction race
		// ahead and forces a pull-back correction on every heartbeat.
		const float lateralSpeed = (flags & movement_flags::WalkMode) ? walkSpeed : runSpeed;
		const float speed = ((flags & movement_flags::Backward) && !(flags & movement_flags::Forward))
			? backwardsSpeed
			: lateralSpeed;

		return dir * speed;
	}

	void RemoteMovementRenderer::StepFallArc(const float deltaTime)
	{
		m_fallVelocity.y   += kGravity * deltaTime;
		m_fallTimeAccumSec += deltaTime;
	}
}
