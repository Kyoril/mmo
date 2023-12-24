
#include "animation_track.h"

#include <ranges>

#include "animation.h"

namespace mmo
{
	namespace
	{
		// Locally key frame search helper
		struct KeyFrameTimeLess
		{
			bool operator() (const KeyFramePtr& kf, const KeyFramePtr& kf2) const
			{
				return kf->GetTime() < kf2->GetTime();
			}
		};
	}

	AnimationTrack::AnimationTrack(Animation& parent, const uint16 handle)
		: m_parent(parent)
		, m_handle(handle)
	{
	}

	uint16 AnimationTrack::GetNumKeyFrames() const
	{
		return static_cast<unsigned short>(m_keyFrames.size());
	}

	KeyFramePtr AnimationTrack::GetKeyFrame(const uint16 index) const
	{
		ASSERT(index < static_cast<uint16>(m_keyFrames.size()));
		return m_keyFrames[index];
	}

	float AnimationTrack::GetKeyFramesAtTime(const TimeIndex& timeIndex, KeyFrame** keyFrame1, KeyFrame** keyFrame2, uint16* firstKeyIndex) const
	{
		// Parametric time
		// t1 = time of previous keyframe
		// t2 = time of next keyframe
		float t2;

		float timePos = timeIndex.GetTimePos();

		// Find first keyframe after or on current time
		KeyFrameList::const_iterator i;
		if (timeIndex.HasKeyIndex())
		{
			// Global keyframe index available, map to local keyframe index directly.
			assert(timeIndex.GetKeyIndex() < m_keyFrameIndexMap.size());
			i = m_keyFrames.begin() + m_keyFrameIndexMap[timeIndex.GetKeyIndex()];
		}
		else
		{
			// Wrap time
			const float totalAnimationLength = m_parent.GetDuration();
			ASSERT(totalAnimationLength > 0.0f && "Invalid animation length!");

			if (timePos > totalAnimationLength && totalAnimationLength > 0.0f)
			{
				timePos = fmod(timePos, totalAnimationLength);
			}
			
			// No global keyframe index, need to search with local keyframes.
			const auto timeKey = std::make_shared<KeyFrame>(nullptr, timePos);
			i = std::ranges::lower_bound(m_keyFrames, timeKey, KeyFrameTimeLess());
		}

		if (i == m_keyFrames.end())
		{
			// There is no keyframe after this time, wrap back to first
			*keyFrame2 = m_keyFrames.front().get();
			t2 = m_parent.GetDuration() + (*keyFrame2)->GetTime();

			// Use last keyframe as previous keyframe
			--i;
		}
		else
		{
			*keyFrame2 = i->get();
			t2 = (*keyFrame2)->GetTime();

			// Find last keyframe before or on current time
			if (i != m_keyFrames.begin() && timePos < (*i)->GetTime())
			{
				--i;
			}
		}

		// Fill index of the first key
		if (firstKeyIndex)
		{
			*firstKeyIndex = static_cast<unsigned short>(std::distance(m_keyFrames.begin(), i));
		}

		*keyFrame1 = i->get();

		if (const float t1 = (*keyFrame1)->GetTime(); t1 == t2)
		{
			// Same KeyFrame (only one)
			return 0.0;
		}
		else
		{
			return (timePos - t1) / (t2 - t1);
		}
	}

	KeyFramePtr AnimationTrack::CreateKeyFrame(const float timePos)
	{
		auto kf = CreateKeyFrameImpl(timePos);

		// Insert just before upper bound
		const auto i = std::ranges::upper_bound(m_keyFrames, kf, KeyFrameTimeLess());
		KeyFramePtr result = *m_keyFrames.insert(i, std::move(kf));

		KeyFrameDataChanged();
		m_parent.KeyFrameListChanged();

		return result;
	}

	void AnimationTrack::RemoveKeyFrame(const uint16 index)
	{
		ASSERT(index < static_cast<uint16>(m_keyFrames.size()));

		auto i = m_keyFrames.begin();
		i += index;

		m_keyFrames.erase(i);

		KeyFrameDataChanged();
		m_parent.KeyFrameListChanged();
	}

	void AnimationTrack::RemoveAllKeyFrames()
	{
		m_keyFrames.clear();

		KeyFrameDataChanged();
		m_parent.KeyFrameListChanged();
	}

	void AnimationTrack::CollectKeyFrameTimes(std::vector<float>& keyFrameTimes)
	{
		for (const auto& mKeyFrame : m_keyFrames)
		{
			float timePos = mKeyFrame->GetTime();
			if (auto it = std::ranges::lower_bound(keyFrameTimes, timePos); it == keyFrameTimes.end() || *it != timePos)
			{
				keyFrameTimes.insert(it, timePos);
			}
		}
	}

	void AnimationTrack::BuildKeyFrameIndexMap(const std::vector<float>& keyFrameTimes)
	{
		m_keyFrameIndexMap.resize(keyFrameTimes.size());

		size_t i = 0, j = 0;
		while (j <= keyFrameTimes.size())
		{
			m_keyFrameIndexMap[j] = static_cast<uint16>(i);
			while (i < m_keyFrames.size() && m_keyFrames[i]->GetTime() <= keyFrameTimes[j])
			{
				++i;
			}

			++j;
		}
	}

	void AnimationTrack::ApplyBaseKeyFrame(const KeyFrame* base)
	{
	}

	void AnimationTrack::PopulateClone(AnimationTrack* clone) const
	{
		for (const auto& mKeyFrame : m_keyFrames)
		{
			KeyFramePtr clonedKeyFrame = mKeyFrame->Clone(clone);
			clone->m_keyFrames.push_back(clonedKeyFrame);
		}
	}

	NodeAnimationTrack::NodeAnimationTrack(Animation& parent, const uint16 handle)
		: AnimationTrack(parent, handle)
		, m_targetNode(nullptr)
		, m_splines(nullptr)
		, m_splineBuildNeeded(false)
		, m_useShortestRotationPath(true)
	{
	}

	NodeAnimationTrack::NodeAnimationTrack(Animation& parent, const uint16 handle, Node& targetNode)
		: AnimationTrack(parent, handle)
		, m_targetNode(&targetNode)
		, m_splines(nullptr)
		, m_splineBuildNeeded(false)
		, m_useShortestRotationPath(true)
	{
	}

	std::shared_ptr<TransformKeyFrame> NodeAnimationTrack::CreateNodeKeyFrame(const float timePos)
	{
		return std::static_pointer_cast<TransformKeyFrame>(CreateKeyFrame(timePos));
	}

	Node* NodeAnimationTrack::GetAssociatedNode() const
	{
		return m_targetNode;
	}

	void NodeAnimationTrack::SetAssociatedNode(Node* node)
	{
		m_targetNode = node;
	}

	void NodeAnimationTrack::ApplyToNode(Node& node, const TimeIndex& timeIndex, float weight, float scale)
	{
		// Nothing to do if no keyframes or zero weight or no node
		if (m_keyFrames.empty() || weight == 0.0f)
		{
			return;
		}

		TransformKeyFrame kf(nullptr, timeIndex.GetTimePos());
		GetInterpolatedKeyFrame(timeIndex, kf);

		// add to existing. Weights are not relative, but treated as absolute multipliers for the animation
		const Vector3 translate = kf.GetTranslate() * weight * scale;
		node.Translate(translate);

		// interpolate between no-rotation and full rotation, to point 'weight', so 0 = no rotate, 1 = full
		Quaternion rotate;
		Animation::RotationInterpolationMode rim = m_parent.GetRotationInterpolationMode();
		if (rim == Animation::RotationInterpolationMode::Linear)
		{
			rotate = Quaternion::NLerp(weight, Quaternion::Identity, kf.GetRotation(), m_useShortestRotationPath);
		}
		else
		{
			rotate = Quaternion::Slerp(weight, Quaternion::Identity, kf.GetRotation(), m_useShortestRotationPath);
		}
		node.Rotate(rotate);

		Vector3 scaleVector = kf.GetScale();
		if (scaleVector != Vector3::UnitScale)
		{
			if (scale != 1.0f)
			{
				scaleVector = Vector3::UnitScale + (scaleVector - Vector3::UnitScale) * scale;
			}
			else if (weight != 1.0f)
			{
				scaleVector = Vector3::UnitScale + (scaleVector - Vector3::UnitScale) * weight;
			}
		}
		node.Scale(scaleVector);
	}

	void NodeAnimationTrack::SetUseShortestRotationPath(bool useShortestPath)
	{
		m_useShortestRotationPath = useShortestPath;
	}

	bool NodeAnimationTrack::UsesShortestRotationPath() const
	{
		return m_useShortestRotationPath;
	}

	void NodeAnimationTrack::GetInterpolatedKeyFrame(const TimeIndex& timeIndex, KeyFrame& kf) const
	{
		auto* kvRet = dynamic_cast<TransformKeyFrame*>(&kf);
		ASSERT(kvRet);

		KeyFrame* kBase1, * kBase2;
		uint16 firstKeyIndex;

		const float t = GetKeyFramesAtTime(timeIndex, &kBase1, &kBase2, &firstKeyIndex);
		const auto k1 = dynamic_cast<TransformKeyFrame*>(kBase1);
		const auto k2 = dynamic_cast<TransformKeyFrame*>(kBase2);

		if (t == 0.0f)
		{
			kvRet->SetRotation(k1->GetRotation());
			kvRet->SetTranslate(k1->GetTranslate());
			kvRet->SetScale(k1->GetScale());
		}
		else
		{
			const Animation::InterpolationMode im = m_parent.GetInterpolationMode();
			const Animation::RotationInterpolationMode rim = m_parent.GetRotationInterpolationMode();
			Vector3 base;
			switch (im)
			{
			case Animation::InterpolationMode::Linear:
				if (rim == Animation::RotationInterpolationMode::Linear)
				{
					kvRet->SetRotation(Quaternion::NLerp(t, k1->GetRotation(),
						k2->GetRotation(), m_useShortestRotationPath));
				}
				else
				{
					kvRet->SetRotation(Quaternion::Slerp(t, k1->GetRotation(),
						k2->GetRotation(), m_useShortestRotationPath));
				}

				base = k1->GetTranslate();
				kvRet->SetTranslate(base + ((k2->GetTranslate() - base) * t));

				base = k1->GetScale();
				kvRet->SetScale(base + ((k2->GetScale() - base) * t));
				break;

			case Animation::InterpolationMode::Spline:
				if (m_splineBuildNeeded)
				{
					BuildInterpolationSplines();
				}

				kvRet->SetRotation(m_splines->rotationSpline.Interpolate(firstKeyIndex, t, m_useShortestRotationPath));
				kvRet->SetTranslate(m_splines->positionSpline.Interpolate(firstKeyIndex, t));
				kvRet->SetScale(m_splines->scaleSpline.Interpolate(firstKeyIndex, t));

				break;
			}
		}
	}

	void NodeAnimationTrack::Apply(const TimeIndex& timeIndex, const float weight, const float scale)
	{
		if (!m_targetNode)
		{
			return;
		}

		ApplyToNode(*m_targetNode, timeIndex, weight, scale);
	}

	void NodeAnimationTrack::KeyFrameDataChanged() const
	{
		m_splineBuildNeeded = true;
	}

	std::shared_ptr<TransformKeyFrame> NodeAnimationTrack::GetNodeKeyFrame(const uint16 index) const
	{
		return std::static_pointer_cast<TransformKeyFrame>(GetKeyFrame(index));
	}

	bool NodeAnimationTrack::HasNonZeroKeyFrames() const
	{
		for (const auto& keyFrame : m_keyFrames)
		{
			// look for keyframes which have any component which is non-zero
			// Since exporters can be a little inaccurate sometimes we use a
			// tolerance value rather than looking for nothing
			const auto kf = std::static_pointer_cast<TransformKeyFrame>(keyFrame);
			Vector3 trans = kf->GetTranslate();
			Vector3 scale = kf->GetScale();
			Vector3 axis;
			Radian angle;
			kf->GetRotation().ToAngleAxis(angle, axis);
			if (constexpr float tolerance = 1e-3f; !trans.IsCloseTo(Vector3::Zero, tolerance) ||
				!scale.IsCloseTo(Vector3::UnitScale, tolerance) ||
				!FloatEqual(angle.GetValueRadians(), 0.0f, tolerance))
			{
				return true;
			}
		}

		return false;
	}

	void NodeAnimationTrack::Optimize()
	{
		// Eliminate duplicate keyframes from 2nd to penultimate keyframe
		// NB only eliminate middle keys from sequences of 5+ identical keyframes
		// since we need to preserve the boundary keys in place, and we need
		// 2 at each end to preserve tangents for spline interpolation
		Vector3 lastTranslate = Vector3::Zero;
		Vector3 lastScale = Vector3::Zero;
		Quaternion lastOrientation;

		auto i = m_keyFrames.begin();
		const Radian rotationTolerance(1e-3f);

		std::list<uint16> removeList;
		uint16 k = 0;
		uint16 dupKfCount = 0;

		for (; i != m_keyFrames.end(); ++i, ++k)
		{
			const auto kf = std::static_pointer_cast<TransformKeyFrame>(*i);
			Vector3 newTranslate = kf->GetTranslate();
			Vector3 newScale = kf->GetScale();
			// Ignore first keyframe; now include the last keyframe as we eliminate
			// only k-2 in a group of 5 to ensure we only eliminate middle keys
			if (Quaternion newOrientation = kf->GetRotation(); i != m_keyFrames.begin() &&
				newTranslate.IsCloseTo(lastTranslate) &&
				newScale.IsCloseTo(lastScale) &&
				newOrientation.Equals(lastOrientation, rotationTolerance))
			{
				++dupKfCount;

				// 4 indicates this is the 5th duplicate keyframe
				if (dupKfCount == 4)
				{
					// remove the 'middle' keyframe
					removeList.push_back(k - 2);
					--dupKfCount;
				}
			}
			else
			{
				// reset
				dupKfCount = 0;
				lastTranslate = newTranslate;
				lastScale = newScale;
				lastOrientation = newOrientation;
			}
		}

		// Now remove keyframes, in reverse order to avoid index revocation
		for (const uint16 & r : std::ranges::reverse_view(removeList))
		{
			RemoveKeyFrame(r);
		}
	}

	NodeAnimationTrack* NodeAnimationTrack::Clone(Animation& newParent) const
	{
		NodeAnimationTrack* newTrack = newParent.CreateNodeTrack(m_handle, m_targetNode);
		newTrack->m_useShortestRotationPath = m_useShortestRotationPath;
		PopulateClone(newTrack);
		return newTrack;
	}

	void NodeAnimationTrack::ApplyBaseKeyFrame(const KeyFrame* base)
	{
		const auto convertedBase = dynamic_cast<const TransformKeyFrame*>(base);

		for (auto& keyFrame : m_keyFrames)
		{
			const auto kf = std::static_pointer_cast<TransformKeyFrame>(keyFrame);
			kf->SetTranslate(kf->GetTranslate() - convertedBase->GetTranslate());
			kf->SetRotation(convertedBase->GetRotation().Inverse() * kf->GetRotation());
			kf->SetScale(kf->GetScale() * (Vector3::UnitScale / convertedBase->GetScale()));
		}
	}

	KeyFramePtr NodeAnimationTrack::CreateKeyFrameImpl(float time)
	{
		return std::make_shared<TransformKeyFrame>(this, time);
	}

	void NodeAnimationTrack::BuildInterpolationSplines() const
	{
		// Allocate splines if not exists
		if (!m_splines)
		{
			m_splines = std::make_unique<Splines>();
		}

		// Cache to register for optimisation
		Splines* splines = m_splines.get();

		// Don't calc automatically, do it on request at the end
		splines->positionSpline.SetAutoCalculate(false);
		splines->rotationSpline.SetAutoCalculate(false);
		splines->scaleSpline.SetAutoCalculate(false);

		splines->positionSpline.Clear();
		splines->rotationSpline.Clear();
		splines->scaleSpline.Clear();

		KeyFrameList::const_iterator iend;
		iend = m_keyFrames.end(); // precall to avoid overhead
		for (auto i = m_keyFrames.begin(); i != iend; ++i)
		{
			const auto kf = std::static_pointer_cast<TransformKeyFrame>(*i);
			splines->positionSpline.AddPoint(kf->GetTranslate());
			splines->rotationSpline.AddPoint(kf->GetRotation());
			splines->scaleSpline.AddPoint(kf->GetScale());
		}

		splines->positionSpline.RecalculateTangents();
		splines->rotationSpline.RecalculateTangents();
		splines->scaleSpline.RecalculateTangents();

		m_splineBuildNeeded = false;
	}
}
