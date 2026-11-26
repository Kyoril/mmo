// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_air_movement.h"

#include <algorithm>

namespace mmo
{
	namespace
	{
		/// Same values as the world server's glide_rules::FallGravity and TerminalFallSpeed.
		constexpr float FallGravity = 19.6f;
		constexpr float TerminalFallSpeed = 55.0f;
	}

	void BotAirMovement::Jump(const Vector3& position, const float jumpSpeed, const float landingHeight)
	{
		m_position = position;
		m_velocity = Vector3(0.0f, jumpSpeed, 0.0f);
		m_landingHeight = landingHeight;
		m_airborne = true;
		m_gliding = false;
	}

	void BotAirMovement::OpenGlider()
	{
		if (m_airborne)
		{
			m_gliding = true;
		}
	}

	void BotAirMovement::CloseGlider()
	{
		m_gliding = false;
	}

	void BotAirMovement::SetGlideDirection(const Vector3& horizontalDirection)
	{
		m_glideDirection = Vector3(horizontalDirection.x, 0.0f, horizontalDirection.z);
	}

	void BotAirMovement::Reset()
	{
		m_airborne = false;
		m_gliding = false;
		m_velocity = Vector3::Zero;
	}

	BotAirMovement::Step BotAirMovement::Advance(const float seconds, const std::vector<UpdraftVolume>& updrafts, const TraversalSettings& settings)
	{
		Step step;
		if (!m_airborne || seconds <= 0.0f)
		{
			step.position = m_position;
			step.velocity = m_velocity;
			return step;
		}

		const Vector3 previousVelocity = m_velocity;
		if (m_gliding)
		{
			m_velocity = StepGlideVelocity(m_velocity, m_glideDirection, GetUpdraftStrength(updrafts, m_position), settings, seconds);
		}
		else
		{
			m_velocity.y = std::max(-TerminalFallSpeed, m_velocity.y - FallGravity * seconds);
		}

		// Average of both velocities: exact for the constant fall gravity.
		m_position += (previousVelocity + m_velocity) * (0.5f * seconds);

		if (m_velocity.y < 0.0f && m_position.y <= m_landingHeight)
		{
			m_position.y = m_landingHeight;
			m_velocity = Vector3::Zero;
			m_airborne = false;
			m_gliding = false;
			step.landed = true;
		}

		step.position = m_position;
		step.velocity = m_velocity;
		return step;
	}
}
