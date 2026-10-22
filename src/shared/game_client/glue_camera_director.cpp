// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "glue_camera_director.h"

#include <algorithm>

namespace mmo
{
	namespace
	{
		Vector3 catmullRom(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float u)
		{
			const float u2 = u * u;
			const float u3 = u2 * u;
			return (p1 * 2.0f
				+ (p2 - p0) * u
				+ (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * u2
				+ (p1 * 3.0f - p0 - p2 * 3.0f + p3) * u3) * 0.5f;
		}
	}

	GlueCameraKey EvaluateGluePath(const std::vector<GlueCameraKey>& keys, float t)
	{
		if (keys.empty())
		{
			return GlueCameraKey{};
		}

		if (keys.size() == 1)
		{
			return keys.front();
		}

		t = std::clamp(t, 0.0f, 1.0f);
		const float eased = t * t * (3.0f - 2.0f * t);

		const int segments = static_cast<int>(keys.size()) - 1;
		const float scaled = eased * static_cast<float>(segments);
		const int segment = std::min(static_cast<int>(scaled), segments - 1);
		const float u = scaled - static_cast<float>(segment);

		const auto key = [&keys](int i) -> const GlueCameraKey&
		{
			return keys[static_cast<size_t>(std::clamp(i, 0, static_cast<int>(keys.size()) - 1))];
		};

		const GlueCameraKey& k0 = key(segment - 1);
		const GlueCameraKey& k1 = key(segment);
		const GlueCameraKey& k2 = key(segment + 1);
		const GlueCameraKey& k3 = key(segment + 2);

		GlueCameraKey result;
		result.position = catmullRom(k0.position, k1.position, k2.position, k3.position, u);
		result.lookAt = catmullRom(k0.lookAt, k1.lookAt, k2.lookAt, k3.lookAt, u);
		return result;
	}

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
