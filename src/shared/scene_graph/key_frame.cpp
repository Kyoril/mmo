
#include "key_frame.h"

#include "animation_track.h"

namespace mmo
{
	KeyFrame::KeyFrame(const AnimationTrack& parent, float time)
		: m_time(time)
	    , m_parentTrack(&parent)
	{
	}

	TransformKeyFrame::TransformKeyFrame(const AnimationTrack& parent, float time)
		: KeyFrame(parent, time)
	{
	}

	void TransformKeyFrame::SetTranslate(const Vector3& trans)
	{
		m_translate = trans;
		m_parentTrack->KeyFrameDataChanged();
	}

	void TransformKeyFrame::SetScale(const Vector3& scale)
	{
		m_scale = scale;
		m_parentTrack->KeyFrameDataChanged();
	}

	void TransformKeyFrame::SetRotation(const Quaternion& rot)
	{
		m_rotation = rot;
		m_parentTrack->KeyFrameDataChanged();
	}

	std::shared_ptr<KeyFrame> TransformKeyFrame::Clone(AnimationTrack& newParent) const
	{
		auto result = std::make_shared<TransformKeyFrame>(newParent, m_time);
		result->m_translate = m_translate;
		result->m_rotation = m_rotation;
		result->m_scale = m_scale;
		return result;
	}

	std::shared_ptr<KeyFrame> KeyFrame::Clone(AnimationTrack& parent) const
	{
		return std::make_shared<KeyFrame>(*m_parentTrack, m_time);
	}

}
