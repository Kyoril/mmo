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
			m_renderedFacing     = info.facing;
			m_blendStartPos      = info.position;
			m_blendTargetPos     = info.position;
			m_blendTimeLeft      = 0.0f;
			m_blendDuration      = 0.0f;

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

		// ── Position correction ───────────────────────────────────────────────
		// Blend from current rendered position to the authoritative position over
		// kCorrectionBlendSec seconds. This hides the snap while converging quickly.
		const float corrLen = (info.position - m_renderedPos).GetLength();
		DLOG("[RemoteMovement] auth update corr=" << corrLen
		    << "m flags=" << log_hex_digit(info.movementFlags));

		if (corrLen < 0.01f)
		{
			// Negligible correction — snap directly, no blend needed.
			m_renderedPos     = info.position;
			m_blendTimeLeft   = 0.0f;
			m_blendDuration   = 0.0f;
		}
		else
		{
			// Start a blend from current rendered pos toward authoritative pos.
			m_blendStartPos   = m_renderedPos;
			m_blendTargetPos  = info.position;
			m_blendDuration   = kCorrectionBlendSec;
			m_blendTimeLeft   = kCorrectionBlendSec;
		}

		m_authPos        = info.position;
		m_authFacing     = info.facing;
		m_authFlags      = info.movementFlags;
		m_authTimestamp  = info.timestamp;
		m_renderedFacing = info.facing;

		// Reset fall state
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

		// ── 2. Dead-reckoning: advance rendered position ──────────────────────
		const Quaternion orientation(m_renderedFacing, Vector3::UnitY);
		const Vector3 forward = orientation * Vector3::UnitX;
		const Vector3 right   = orientation * Vector3::UnitZ;

		if (isFalling)
		{
			const Vector3 lateral = ComputeVelocity(forward, right, m_authFlags, runSpeed, backwardsSpeed);
			m_renderedPos.x += lateral.x * deltaTime;
			m_renderedPos.z += lateral.z * deltaTime;
			StepFallArc(deltaTime);
			m_renderedPos.y += m_fallVelocity.y * deltaTime;
		}
		else
		{
			const Vector3 vel = ComputeVelocity(forward, right, m_authFlags, runSpeed, backwardsSpeed);
			m_renderedPos += vel * deltaTime;
		}

		// ── 3. Blend correction ───────────────────────────────────────────────
		// While a blend is active, interpolate the VISUAL position from the
		// pre-correction rendered pos toward the current dead-reckoned position.
		// This smooths out the position pop when an authoritative update arrives.
		Vector3 visualPos = m_renderedPos;
		if (m_blendTimeLeft > 0.0f)
		{
			m_blendTimeLeft = std::max(0.0f, m_blendTimeLeft - deltaTime);
			const float t = (m_blendDuration > 0.0f)
				? 1.0f - (m_blendTimeLeft / m_blendDuration)
				: 1.0f;
			// Blend from blendStart toward the current (dead-reckoned) authoritative pos
			visualPos = m_blendStartPos + (m_renderedPos - m_blendStartPos) * t;
		}

		// ── 4. Fill output ────────────────────────────────────────────────────
		outState.position      = visualPos;
		outState.facing        = m_renderedFacing;
		outState.movementFlags = m_authFlags;
		outState.isFalling     = isFalling;
		outState.velocity      = isFalling ? m_fallVelocity : ComputeVelocity(forward, right, m_authFlags, runSpeed, backwardsSpeed);

		return true;
	}

	void RemoteMovementRenderer::Reset()
	{
		m_initialized        = false;
		m_blendTimeLeft      = 0.0f;
		m_blendDuration      = 0.0f;
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
