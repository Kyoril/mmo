// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "vigor_tracker.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mmo
{
	namespace
	{
		/// Vigor at or below this counts as empty. Settling in float steps can leave a tiny rest.
		constexpr float EmptyEpsilon = 0.001f;

		/// Converts seconds from now into an absolute time, rounded up so that Vigor has reached the
		/// predicted value by then.
		GameTime SecondsFromNow(const GameTime now, const float seconds)
		{
			if (!(seconds > 0.0f))
			{
				return now;
			}

			return now + static_cast<GameTime>(std::ceil(static_cast<double>(seconds) * 1000.0));
		}
	}

	VigorTracker::VigorTracker(const TraversalSettings& settings)
		: m_settings(settings)
	{
		m_state.max = m_settings.maxVigor;
		m_state.current = m_state.max;
		m_state.secondsSinceDrain = m_settings.regenDelaySeconds;
	}

	void VigorTracker::Reset(const GameTime now, const float maxVigor)
	{
		m_lastSettle = now;
		m_state.max = std::max(0.0f, maxVigor);
		m_state.current = m_state.max;
		m_state.secondsSinceDrain = m_settings.regenDelaySeconds;
		m_gliding = false;
		m_inUpdraft = false;
		m_grounded = true;
	}

	void VigorTracker::SetMax(const GameTime now, const float maxVigor)
	{
		Settle(now);

		const float newMax = std::max(0.0f, maxVigor);
		const bool hadNone = m_state.max <= 0.0f;
		m_state.max = newMax;
		m_state.current = hadNone ? newMax : std::clamp(m_state.current, 0.0f, newMax);
	}

	void VigorTracker::Settle(const GameTime now)
	{
		if (now <= m_lastSettle)
		{
			return;
		}

		Advance(m_state, now);
		m_lastSettle = now;
	}

	void VigorTracker::SetGliding(const GameTime now, const bool gliding, const bool inUpdraft)
	{
		Settle(now);

		if (gliding && !m_gliding)
		{
			m_state.current = std::max(0.0f, m_state.current - m_settings.glideStartCost);
			m_state.secondsSinceDrain = 0.0f;
		}

		m_gliding = gliding;
		m_inUpdraft = gliding && inUpdraft;
	}

	void VigorTracker::SetExtraDrain(const GameTime now, const float perSecond)
	{
		Settle(now);
		m_extraDrain = std::max(0.0f, perSecond);
	}

	void VigorTracker::SetInCombat(const GameTime now, const bool inCombat)
	{
		Settle(now);
		m_inCombat = inCombat;
	}

	void VigorTracker::SetGrounded(const GameTime now, const bool grounded)
	{
		Settle(now);
		m_grounded = grounded;
	}

	float VigorTracker::GetCurrent(const GameTime now) const
	{
		VigorState state = m_state;
		Advance(state, now);
		return state.current;
	}

	bool VigorTracker::IsEmpty(const GameTime now) const
	{
		return GetCurrent(now) <= EmptyEpsilon;
	}

	GameTime VigorTracker::GetEmptyTime(const GameTime now) const
	{
		if (!IsDraining())
		{
			return 0;
		}

		VigorState state = m_state;
		Advance(state, now);
		if (state.current <= EmptyEpsilon)
		{
			return now;
		}

		return SecondsFromNow(now, SecondsUntilEmpty(state, m_settings, GetActivity(), m_extraDrain));
	}

	GameTime VigorTracker::GetFullTime(const GameTime now) const
	{
		if (IsDraining() || m_inCombat || !m_grounded || m_settings.regenPerSecond <= 0.0f)
		{
			return 0;
		}

		VigorState state = m_state;
		Advance(state, now);
		if (state.current >= state.max)
		{
			return 0;
		}

		const float delayLeft = std::max(0.0f, m_settings.regenDelaySeconds - state.secondsSinceDrain);
		return SecondsFromNow(now, delayLeft + (state.max - state.current) / m_settings.regenPerSecond);
	}

	bool VigorTracker::IsDraining() const
	{
		return m_gliding || m_extraDrain > 0.0f;
	}

	VigorActivity VigorTracker::GetActivity() const
	{
		if (m_gliding)
		{
			return m_inUpdraft ? VigorActivity::GlidingInUpdraft : VigorActivity::Gliding;
		}

		return m_extraDrain > 0.0f ? VigorActivity::Draining : VigorActivity::Idle;
	}

	void VigorTracker::Advance(VigorState& state, const GameTime now) const
	{
		if (now <= m_lastSettle)
		{
			return;
		}

		const float seconds = static_cast<float>(static_cast<double>(now - m_lastSettle) / 1000.0);
		AdvanceVigor(state, m_settings, GetActivity(), m_extraDrain, m_inCombat, m_grounded, seconds);
	}
}
