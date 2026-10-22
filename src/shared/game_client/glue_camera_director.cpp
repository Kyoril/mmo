// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "glue_camera_director.h"

#include <algorithm>

namespace mmo
{
	GlueCameraDirector::GlueCameraDirector(float flightSeconds, float flyOutSeconds)
		: m_flightSeconds(std::max(flightSeconds, 0.001f))
		, m_flyOutSeconds(flyOutSeconds > 0.0f ? std::max(flyOutSeconds, 0.001f) : std::max(flightSeconds, 0.001f))
	{
	}

	void GlueCameraDirector::FlyIn()
	{
		if (m_mode == GlueCameraMode::Plaza || m_mode == GlueCameraMode::FlyingIn)
		{
			return;
		}

		m_mode = GlueCameraMode::FlyingIn;
	}

	void GlueCameraDirector::FlyOut()
	{
		if (m_mode == GlueCameraMode::Gate || m_mode == GlueCameraMode::FlyingOut)
		{
			return;
		}

		m_mode = GlueCameraMode::FlyingOut;
	}

	void GlueCameraDirector::Skip()
	{
		if (m_mode == GlueCameraMode::FlyingIn)
		{
			m_t = 1.0f;
			m_mode = GlueCameraMode::Plaza;
			m_finished = true;
		}
		else if (m_mode == GlueCameraMode::FlyingOut)
		{
			m_t = 0.0f;
			m_mode = GlueCameraMode::Gate;
			m_finished = true;
		}
	}

	void GlueCameraDirector::SnapToPlaza()
	{
		m_t = 1.0f;
		m_mode = GlueCameraMode::Plaza;
	}

	void GlueCameraDirector::SnapToGate()
	{
		m_t = 0.0f;
		m_mode = GlueCameraMode::Gate;
	}

	void GlueCameraDirector::Update(float deltaSeconds)
	{
		const float elapsed = std::max(deltaSeconds, 0.0f);

		if (m_mode == GlueCameraMode::FlyingIn)
		{
			m_t += elapsed / m_flightSeconds;
			if (m_t >= 1.0f)
			{
				m_t = 1.0f;
				m_mode = GlueCameraMode::Plaza;
				m_finished = true;
			}
		}
		else if (m_mode == GlueCameraMode::FlyingOut)
		{
			m_t -= elapsed / m_flyOutSeconds;
			if (m_t <= 0.0f)
			{
				m_t = 0.0f;
				m_mode = GlueCameraMode::Gate;
				m_finished = true;
			}
		}
	}

	bool GlueCameraDirector::ConsumeFlightFinished()
	{
		const bool result = m_finished;
		m_finished = false;
		return result;
	}
}
