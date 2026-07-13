// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "animation_blend_space.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace
	{
		float NormalizeAngle(float degrees)
		{
			degrees = std::fmod(degrees, 360.0f);
			if (degrees < 0.0f)
			{
				degrees += 360.0f;
			}
			return degrees;
		}

		/// Circular distance between two angles in [0, 360), result in [0, 180].
		float CircularDistance(const float a, const float b)
		{
			const float diff = std::fabs(a - b);
			return diff > 180.0f ? 360.0f - diff : diff;
		}
	}

	void AnimationBlendSpace::Clear()
	{
		m_samples.clear();
	}

	void AnimationBlendSpace::AddSample(const float angleDegrees, AnimationState& state)
	{
		const float angle = NormalizeAngle(angleDegrees);
		for (Sample& sample : m_samples)
		{
			if (std::fabs(sample.angle - angle) < 0.01f)
			{
				sample.state = &state;
				return;
			}
		}

		m_samples.push_back(Sample{ angle, &state });
	}

	void AnimationBlendSpace::Finalize()
	{
		std::sort(m_samples.begin(), m_samples.end(),
			[](const Sample& a, const Sample& b) { return a.angle < b.angle; });
	}

	uint32 AnimationBlendSpace::Evaluate(const float angleDegrees, WeightedAnimClip out[2]) const
	{
		if (m_samples.empty())
		{
			return 0;
		}

		const float angle = NormalizeAngle(angleDegrees);

		if (m_samples.size() == 1)
		{
			out[0] = WeightedAnimClip{ m_samples.front().state, 1.0f };
			return 1;
		}

		// Find the two circularly adjacent samples around the requested angle. m_samples is
		// sorted, so "lower" is the last sample with angle <= requested (wrapping to the last
		// sample when the angle lies before the first one).
		size_t lowerIndex = m_samples.size() - 1;
		for (size_t i = 0; i < m_samples.size(); ++i)
		{
			if (m_samples[i].angle <= angle)
			{
				lowerIndex = i;
			}
			else
			{
				break;
			}
		}
		const size_t upperIndex = (lowerIndex + 1) % m_samples.size();

		const Sample& lower = m_samples[lowerIndex];
		const Sample& upper = m_samples[upperIndex];

		// Exact hit on a sample: play it alone.
		if (CircularDistance(lower.angle, angle) < 0.01f)
		{
			out[0] = WeightedAnimClip{ lower.state, 1.0f };
			return 1;
		}
		if (CircularDistance(upper.angle, angle) < 0.01f)
		{
			out[0] = WeightedAnimClip{ upper.state, 1.0f };
			return 1;
		}

		// Angular span from lower to upper going clockwise (the direction the request lies in).
		float span = upper.angle - lower.angle;
		if (span <= 0.0f)
		{
			span += 360.0f;
		}

		// Sparse coverage: snap to the nearest sample instead of blending across a wide gap
		// (e.g. only Run at 0 and RunBack at 180 exist).
		if (span > m_maxBlendAngle)
		{
			const Sample& nearest =
				CircularDistance(lower.angle, angle) <= CircularDistance(upper.angle, angle) ? lower : upper;
			out[0] = WeightedAnimClip{ nearest.state, 1.0f };
			return 1;
		}

		float offset = angle - lower.angle;
		if (offset < 0.0f)
		{
			offset += 360.0f;
		}

		const float t = offset / span;
		out[0] = WeightedAnimClip{ lower.state, 1.0f - t };
		out[1] = WeightedAnimClip{ upper.state, t };
		return 2;
	}
}
