// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "plaza_interaction.h"

#include "math/degree.h"
#include "math/math_utils.h"
#include "math/radian.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace mmo
{
	namespace
	{
		float smoothStep(const float t)
		{
			const float clamped = std::clamp(t, 0.0f, 1.0f);
			return clamped * clamped * (3.0f - 2.0f * clamped);
		}

		/// Wraps degrees into (-180, 180].
		float wrapDegrees(float degrees)
		{
			degrees = std::fmod(degrees, 360.0f);
			if (degrees <= -180.0f)
			{
				degrees += 360.0f;
			}
			else if (degrees > 180.0f)
			{
				degrees -= 360.0f;
			}

			return degrees;
		}

		bool containsNoCase(const String& text, const char* needle)
		{
			String lower = text;
			std::transform(lower.begin(), lower.end(), lower.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return lower.find(needle) != String::npos;
		}
	}

	int32 PickNearestPlazaHit(const std::vector<PlazaPickHit>& hits)
	{
		int32 best = -1;
		float bestDistance = 0.0f;

		for (const PlazaPickHit& hit : hits)
		{
			if (!hit.pickable || hit.orderIndex < 0)
			{
				continue;
			}

			if (best < 0 || hit.distance < bestDistance)
			{
				best = hit.orderIndex;
				bestDistance = hit.distance;
			}
		}

		return best;
	}

	float PlazaTurnTowardsDegrees(const float yawDegrees, const Vector3& position, const Vector3& cameraPosition, const float maxTurnDegrees)
	{
		const float dx = cameraPosition.x - position.x;
		const float dz = cameraPosition.z - position.z;
		if (dx * dx + dz * dz < 0.0001f)
		{
			return 0.0f;
		}

		const float towardsCamera = Radian(DirectionToFacing(dx, dz)).GetValueDegrees();
		const float turn = wrapDegrees(towardsCamera - yawDegrees);
		return std::clamp(turn, -maxTurnDegrees, maxTurnDegrees);
	}

	float PlazaGreetingWeight(const float elapsedSeconds, const float lengthSeconds, const float fadeInSeconds, const float fadeOutSeconds)
	{
		if (lengthSeconds <= 0.0f || elapsedSeconds < 0.0f || elapsedSeconds >= lengthSeconds)
		{
			return 0.0f;
		}

		const float in = fadeInSeconds > 0.0f ? std::min(1.0f, elapsedSeconds / fadeInSeconds) : 1.0f;
		const float out = fadeOutSeconds > 0.0f ? std::min(1.0f, (lengthSeconds - elapsedSeconds) / fadeOutSeconds) : 1.0f;
		return std::clamp(std::min(in, out), 0.0f, 1.0f);
	}

	float PlazaRotationReturnDegrees(const float startDegrees, const float elapsedSeconds, const float durationSeconds)
	{
		if (durationSeconds <= 0.0f || elapsedSeconds >= durationSeconds)
		{
			return 0.0f;
		}

		return startDegrees * (1.0f - smoothStep(elapsedSeconds / durationSeconds));
	}

	bool IsPlazaSeatedPose(const String& pose)
	{
		return containsNoCase(pose, "sit") || containsNoCase(pose, "sleep") || containsNoCase(pose, "kneel");
	}

	bool PlazaGreetingBlend::Start(const float lengthSeconds, const float fadeInSeconds, const float fadeOutSeconds)
	{
		if (m_playing)
		{
			// Resume: the weight recovers from where it is, so nothing pops.
			m_released = false;
			return false;
		}

		m_playing = lengthSeconds > 0.0f;
		m_released = false;
		m_elapsed = 0.0f;
		m_length = lengthSeconds;
		m_fadeIn = fadeInSeconds;
		m_fadeOut = fadeOutSeconds;
		m_weight = 0.0f;
		return m_playing;
	}

	void PlazaGreetingBlend::Release()
	{
		if (m_playing)
		{
			m_released = true;
		}
	}

	void PlazaGreetingBlend::Update(const float deltaSeconds)
	{
		if (!m_playing)
		{
			return;
		}

		const float delta = std::max(deltaSeconds, 0.0f);
		m_elapsed += delta;
		if (m_elapsed >= m_length)
		{
			Reset();
			return;
		}

		if (m_released)
		{
			m_weight = m_fadeOut > 0.0f ? m_weight - delta / m_fadeOut : 0.0f;
			if (m_weight <= 0.0f)
			{
				Reset();
			}
			return;
		}

		// Follows the clip's envelope, but never rises faster than the fade in (a resumed greeting
		// climbs back from its faded weight).
		const float envelope = PlazaGreetingWeight(m_elapsed, m_length, m_fadeIn, m_fadeOut);
		const float rise = m_fadeIn > 0.0f ? m_weight + delta / m_fadeIn : 1.0f;
		m_weight = std::clamp(std::min(envelope, rise), 0.0f, 1.0f);
	}

	void PlazaGreetingBlend::Reset()
	{
		m_playing = false;
		m_released = false;
		m_elapsed = 0.0f;
		m_weight = 0.0f;
	}

	bool PlazaHoverReaction::Update(const bool active, const float deltaSeconds)
	{
		const bool entered = active && !m_active;
		m_active = active;

		const float glowStep = deltaSeconds / GlowFadeSeconds;
		const float turnStep = deltaSeconds / TurnSeconds;
		if (active)
		{
			m_glow = std::min(1.0f, m_glow + glowStep);
			m_turn = std::min(1.0f, m_turn + turnStep);
		}
		else
		{
			m_glow = std::max(0.0f, m_glow - glowStep);
			m_turn = std::max(0.0f, m_turn - turnStep);
		}

		return entered;
	}

	void PlazaHoverReaction::Reset()
	{
		m_active = false;
		m_glow = 0.0f;
		m_turn = 0.0f;
	}

	float PlazaHoverReaction::GetGlow() const
	{
		return smoothStep(m_glow);
	}

	float PlazaHoverReaction::GetTurn() const
	{
		return smoothStep(m_turn);
	}
}
