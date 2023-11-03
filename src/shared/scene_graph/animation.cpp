
#include "animation.h"

#include <cmath>

namespace mmo
{
	Animation::InterpolationMode Animation::s_defaultInterpolationMode = Animation::InterpolationMode::Linear;
	Animation::RotationInterpolationMode Animation::s_defaultRotationInterpolationMode = Animation::RotationInterpolationMode::Linear;

	Animation::Animation(String name, const float duration)
		: m_name(std::move(name))
		, m_duration(duration)
		, m_interpolationMode(s_defaultInterpolationMode)
		, m_rotationInterpolationMode(s_defaultRotationInterpolationMode)
		, m_useBaseKeyFrame(false)
		, m_baseKeyFrameTime(0.0f)
		, m_container(nullptr)
	{
	}

	Animation::~Animation()
	{
		DestroyAllTracks();
	}

	void Animation::Apply(float timePos, float weight, float scale)
	{
		ApplyBaseKeyFrame();


	}

	void Animation::Apply(Skeleton& skeleton, float timePos, float weight, float scale)
	{
		ApplyBaseKeyFrame();


	}

	TimeIndex Animation::GetTimeIndex(float timePos) const
	{
		if (m_keyFrameTimesDirty)
		{
			BuildKeyFrameTimeList();
		}


		float totalAnimationLength = m_duration;

		if (timePos > totalAnimationLength && totalAnimationLength > 0.0f)
		{
			timePos = std::fmod(timePos, totalAnimationLength);
		}

		// Search for global index
		const auto it = std::lower_bound(m_keyFrameTimes.begin(), m_keyFrameTimes.end(), timePos);
		return {timePos, static_cast<uint32>(std::distance(m_keyFrameTimes.begin(), it))};
	}

	void Animation::DestroyAllTracks()
	{
	}

	void Animation::BuildKeyFrameTimeList() const
	{
		// Clear old keyframe times
		m_keyFrameTimes.clear();

		// TODO: Collect all keyframe times from each track

		// TODO: Build global index to local index map for each track

		// Reset dirty flag
		m_keyFrameTimesDirty = false;
	}
}
