// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "vigor_predictor.h"

#include <algorithm>

namespace mmo
{
	VigorPredictor::VigorPredictor(TraversalSettings settings)
		: m_settings(settings)
	{
	}

	void VigorPredictor::Anchor(const float value, const float maxValue, const GameTime now)
	{
		// A first value has nothing to drop from.
		const float previous = m_anchored ? m_state.current : value;

		if (!m_anchored)
		{
			// Nothing drained since the character appeared: regeneration may start at once, as on
			// the server (VigorTracker starts with the delay elapsed).
			m_state.secondsSinceDrain = m_settings.regenDelaySeconds;
			m_anchored = true;
		}

		m_state.current = value;
		m_state.max = maxValue;
		m_lastTime = now;

		UpdateEmptyFlag(previous);
	}

	void VigorPredictor::Advance(const GameTime now, const VigorActivity activity, const float extraDrainPerSecond, const bool inCombat, const bool grounded)
	{
		// Opening the glider costs TraversalSettings::glideStartCost at once, like on the server
		// (VigorTracker::SetGliding). The server's next write already contains the cost.
		const bool gliding = activity == VigorActivity::Gliding || activity == VigorActivity::GlidingInUpdraft;
		const bool glideStarted = gliding && !m_wasGliding;
		m_wasGliding = gliding;

		if (!m_anchored)
		{
			return;
		}

		const float previous = m_state.current;

		if (now > m_lastTime)
		{
			// Absolute times stay integer; only the step becomes a float.
			const float seconds = static_cast<float>(now - m_lastTime) / 1000.0f;
			m_lastTime = now;

			AdvanceVigor(m_state, m_settings, activity, extraDrainPerSecond, inCombat, grounded, seconds);
		}

		if (glideStarted)
		{
			m_state.current = std::max(0.0f, m_state.current - m_settings.glideStartCost);
			m_state.secondsSinceDrain = 0.0f;
		}

		UpdateEmptyFlag(previous);
	}

	bool VigorPredictor::ConsumeBecameEmpty()
	{
		const bool result = m_becameEmpty;
		m_becameEmpty = false;
		return result;
	}

	bool VigorPredictor::ConsumeChangeNotification(const GameTime now)
	{
		if (!m_anchored)
		{
			return false;
		}

		if (m_hasNotified && m_notifiedValue == m_state.current && m_notifiedMax == m_state.max)
		{
			return false;
		}

		if (m_hasNotified && now < m_lastNotifyTime + vigorChangeNotifyIntervalMs)
		{
			return false;
		}

		m_hasNotified = true;
		m_notifiedValue = m_state.current;
		m_notifiedMax = m_state.max;
		m_lastNotifyTime = now;
		return true;
	}

	void VigorPredictor::UpdateEmptyFlag(const float previous)
	{
		if (m_state.current > 0.0f)
		{
			// Above zero again: a report that was not consumed yet is stale.
			m_becameEmpty = false;
			return;
		}

		if (previous > 0.0f)
		{
			m_becameEmpty = true;
		}
	}
}
