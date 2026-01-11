
#include "animation.h"

#include <cmath>
#include <ranges>

#include "bone.h"
#include "skeleton.h"

namespace mmo
{
	Animation::InterpolationMode Animation::s_defaultInterpolationMode = InterpolationMode::Linear;
	Animation::RotationInterpolationMode Animation::s_defaultRotationInterpolationMode = RotationInterpolationMode::Linear;

	Animation::Animation(String name, const float duration)
		: m_name(std::move(name))
		, m_duration(duration)
		, m_interpolationMode(s_defaultInterpolationMode)
		, m_rotationInterpolationMode(s_defaultRotationInterpolationMode)
		, m_keyFrameTimesDirty(false)
		, m_useBaseKeyFrame(false)
		, m_baseKeyFrameTime(0.0f)
		, m_container(nullptr)
	{
	}

	Animation::~Animation()
	{
		DestroyAllTracks();
	}

	void Animation::Apply(const float timePos, const float weight, const float scale)
	{
		ApplyBaseKeyFrame();

		// Calculate time index for fast keyframe search
		const TimeIndex timeIndex = GetTimeIndex(timePos);

		for (const auto& nodeTrack : m_nodeTrackList | std::views::values)
		{
			nodeTrack->Apply(timeIndex, weight, scale);
		}
	}

	void Animation::ApplyToNode(Node* node, const float timePos, const float weight, const float scale)
	{
		ApplyBaseKeyFrame();

		// Calculate time index for fast keyframe search
		const TimeIndex timeIndex = GetTimeIndex(timePos);

		for (const auto& nodeTrack : m_nodeTrackList | std::views::values)
		{
			nodeTrack->ApplyToNode(*node, timeIndex, weight, scale);
		}
	}

	void Animation::Apply(const Skeleton& skeleton, const float timePos, const float weight, const float scale)
	{
		ApplyBaseKeyFrame();

		// Calculate time index for fast keyframe search
		const TimeIndex timeIndex = GetTimeIndex(timePos);

		for (const auto& [boneHandle, nodeTrack] : m_nodeTrackList)
		{
			Bone* b = skeleton.GetBone(boneHandle);
			nodeTrack->ApplyToNode(*b, timeIndex, weight, scale);
		}
	}

	void Animation::Apply(const Skeleton& skeleton, const float timePos, const float weight, const AnimationState::BoneBlendMask& blendMask, const float scale)
	{
		ApplyBaseKeyFrame();

		// Calculate time index for fast keyframe search
		const TimeIndex timeIndex = GetTimeIndex(timePos);

		for (const auto& [boneHandle, animationTrack] : m_nodeTrackList)
		{
			// get bone to apply to 
			Bone* b = skeleton.GetBone(boneHandle);
			animationTrack->ApplyToNode(*b, timeIndex, blendMask[b->GetHandle()] * weight, scale);
		}
	}

	TimeIndex Animation::GetTimeIndex(float timePos) const
	{
		if (m_keyFrameTimesDirty)
		{
			BuildKeyFrameTimeList();
		}

		if (const float totalAnimationLength = m_duration; timePos > totalAnimationLength && totalAnimationLength > 0.0f)
		{
			timePos = std::fmod(timePos, totalAnimationLength);
		}

		// Search for global index
		const auto it = std::ranges::lower_bound(m_keyFrameTimes, timePos);
		return {timePos, static_cast<uint32>(std::distance(m_keyFrameTimes.begin(), it))};
	}

	bool Animation::HasNodeTrack(const uint16 handle) const
	{
		return m_nodeTrackList.contains(handle);
	}

	NodeAnimationTrack* Animation::CreateNodeTrack(const uint16 handle)
	{
		ASSERT(!HasNodeTrack(handle));

		return (m_nodeTrackList[handle] = std::make_unique<NodeAnimationTrack>(*this, handle)).get();
	}

	NodeAnimationTrack* Animation::CreateNodeTrack(const uint16 handle, Node* node)
	{
		NodeAnimationTrack* ret = CreateNodeTrack(handle);
		ret->SetAssociatedNode(node);

		return ret;
	}

	uint16 Animation::GetNumNodeTracks() const
	{
		return static_cast<uint16>(m_nodeTrackList.size());
	}

	NodeAnimationTrack* Animation::GetNodeTrack(const uint16 handle) const
	{
		if (const auto it = m_nodeTrackList.find(handle); it != m_nodeTrackList.end())
		{
			return it->second.get();
		}

		return nullptr;
	}

	void Animation::DestroyAllNodeTracks()
	{
		m_nodeTrackList.clear();
		KeyFrameListChanged();
	}

	void Animation::DestroyAllTracks()
	{
		DestroyAllNodeTracks();
	}

	void Animation::SetUseBaseKeyFrame(const bool useBaseKeyFrame, const float keyframeTime, const String& baseAnimName)
	{
		if (useBaseKeyFrame != m_useBaseKeyFrame || 
			keyframeTime != m_baseKeyFrameTime ||
			baseAnimName != m_baseKeyFrameAnimationName)
		{
			m_useBaseKeyFrame = useBaseKeyFrame;
			m_baseKeyFrameTime = keyframeTime;
			m_baseKeyFrameAnimationName = baseAnimName;
		}
	}

	void Animation::ApplyBaseKeyFrame()
	{
		if (m_useBaseKeyFrame)
		{
			Animation* baseAnim = this;
			if (!m_baseKeyFrameAnimationName.empty() && m_container)
			{
				baseAnim = m_container->GetAnimation(m_baseKeyFrameAnimationName);
			}

			if (baseAnim)
			{
				for (auto& trackPtr : m_nodeTrackList | std::views::values)
				{
					NodeAnimationTrack* track = trackPtr.get();
					NodeAnimationTrack* baseTrack;
					if (baseAnim == this)
					{
						baseTrack = track;
					}
					else
					{
						baseTrack = baseAnim->GetNodeTrack(track->GetHandle());
					}
					
					TransformKeyFrame kf(baseTrack, m_baseKeyFrameTime);
					baseTrack->GetInterpolatedKeyFrame(baseAnim->GetTimeIndex(m_baseKeyFrameTime), kf);
					track->ApplyBaseKeyFrame(&kf);
				}
			}

			// Re-base has been done, this is a one-way translation
			m_useBaseKeyFrame = false;
		}
	}

	void Animation::DestroyNodeTrack(const uint16 handle)
	{
		if (const auto it = m_nodeTrackList.find(handle); it != m_nodeTrackList.end())
		{
			m_nodeTrackList.erase(it);
			KeyFrameListChanged();
		}
	}

	void Animation::Optimize(const bool discardIdentityNodeTracks)
	{
		OptimizeNodeTracks(discardIdentityNodeTracks);
	}

	Animation* Animation::Clone(const String& newName)
	{
		TODO("Implement");
		return nullptr;
	}

	void Animation::BuildKeyFrameTimeList() const
	{
		// Clear old keyframe times
		m_keyFrameTimes.clear();

		// Collect all keyframe times from each track
		for (const auto& nodeTrack : m_nodeTrackList | std::views::values)
		{
			nodeTrack->CollectKeyFrameTimes(m_keyFrameTimes);
		}

		// Build global index to local index map for each track
		for (const auto& nodeTrack : m_nodeTrackList | std::views::values)
		{
			nodeTrack->BuildKeyFrameIndexMap(m_keyFrameTimes);
		}

		// Reset dirty flag
		m_keyFrameTimesDirty = false;
	}

	void Animation::OptimizeNodeTracks(const bool discardIdentityTracks)
	{
		// Iterate over the node tracks and identify those with no useful keyframes
		std::list<uint16> tracksToDestroy;
		NodeTrackList::iterator i;
		for (auto& i : m_nodeTrackList)
		{
			if (NodeAnimationTrack* track = i.second.get(); discardIdentityTracks && !track->HasNonZeroKeyFrames())
			{
				// mark the entire track for destruction
				tracksToDestroy.push_back(i.first);
			}
			else
			{
				track->Optimize();
			}

		}

		// Now destroy the tracks we marked for death
		for (const uint16& handle : tracksToDestroy)
		{
			DestroyNodeTrack(handle);
		}
	}

	void Animation::AddNotify(std::unique_ptr<AnimationNotify> notify)
	{
		if (notify)
		{
			m_notifies.push_back(std::move(notify));
		}
	}

	void Animation::RemoveNotify(const size_t index)
	{
		if (index < m_notifies.size())
		{
			m_notifies.erase(m_notifies.begin() + index);
		}
	}

	void Animation::ClearNotifies()
	{
		m_notifies.clear();
	}

	AnimationNotify* Animation::GetNotify(const size_t index) const
	{
		if (index < m_notifies.size())
		{
			return m_notifies[index].get();
		}
		return nullptr;
	}
}
