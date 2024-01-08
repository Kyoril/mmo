#pragma once

#include <map>
#include <vector>

#include "animation_state.h"
#include "animation_track.h"
#include "base/typedefs.h"

namespace mmo
{
	class Entity;
	class Animation;
	class Skeleton;

	class AnimationContainer
	{
	public:
		virtual ~AnimationContainer() = default;

	public:
		virtual uint16 GetNumAnimations() const = 0;

		virtual Animation* GetAnimation(uint16 index) const = 0;

		virtual Animation* GetAnimation(const String& name) const = 0;

		virtual Animation& CreateAnimation(const String& name, float duration) = 0;

		virtual bool HasAnimation(const String& name) const = 0;

		virtual void RemoveAnimation(const String& name) = 0;
	};

	class Animation
	{
	public:
		typedef std::map<uint16, std::unique_ptr<NodeAnimationTrack>> NodeTrackList;

		enum class InterpolationMode
		{
			Linear,
			Spline
		};

		enum class RotationInterpolationMode
		{
			Linear,
			Spherical
		};

	public:
		Animation(String name, float duration);
		virtual ~Animation();

	public:
		const String& GetName() const { return m_name; }

		float GetDuration() const { return m_duration; }

		void SetDuration(const float duration) { m_duration = duration; }

		void Apply(float timePos, float weight = 1.0f, float scale = 1.0f);

		void ApplyToNode(Node* node, float timePos, float weight = 1.0, float scale = 1.0f);

		void Apply(const Skeleton& skeleton, float timePos, float weight = 1.0f, float scale = 1.0f);

		void Apply(const Skeleton& skeleton, float timePos, float weight, const AnimationState::BoneBlendMask& blendMask, float scale);

		TimeIndex GetTimeIndex(float timePos) const;

		bool HasNodeTrack(uint16 handle) const;

		NodeAnimationTrack* CreateNodeTrack(uint16 handle);

		NodeAnimationTrack* CreateNodeTrack(uint16 handle, Node* node);

		uint16 GetNumNodeTracks() const;

		NodeAnimationTrack* GetNodeTrack(uint16 handle) const;

		void KeyFrameListChanged() const { m_keyFrameTimesDirty = true; }

		void DestroyAllNodeTracks();
		void DestroyAllTracks();

		void SetUseBaseKeyFrame(bool useBaseKeyFrame, float keyframeTime, const String& baseAnimName);

		bool GetUseBaseKeyFrame() const { return m_useBaseKeyFrame; }

		float GetBaseKeyFrameTime() const { return m_baseKeyFrameTime; }

		const String& GetBaseKeyFrameAnimationName() const { return m_baseKeyFrameAnimationName; }

		const NodeTrackList& GetNodeTrackList() const { return m_nodeTrackList; }

		void SetInterpolationMode(const InterpolationMode mode) { m_interpolationMode = mode; }

		InterpolationMode GetInterpolationMode() const { return m_interpolationMode; }

		void SetRotationInterpolationMode(const RotationInterpolationMode mode) { m_rotationInterpolationMode = mode; }

		RotationInterpolationMode GetRotationInterpolationMode() const { return m_rotationInterpolationMode; }

		void ApplyBaseKeyFrame();

		void NotifyContainer(AnimationContainer* container) { m_container = container; }

		AnimationContainer* GetContainer() const { return m_container; }

		void DestroyNodeTrack(uint16 handle);

	public:
		static void SetDefaultInterpolationMode(InterpolationMode mode) { s_defaultInterpolationMode = mode; }

		static InterpolationMode GetDefaultInterpolationMode() { return s_defaultInterpolationMode; }

		static void SetDefaultRotationInterpolationMode(const RotationInterpolationMode mode) { s_defaultRotationInterpolationMode = mode; }

		static RotationInterpolationMode GetDefaultRotationInterpolationMode() { return s_defaultRotationInterpolationMode; }

	public:

		void Optimize(bool discardIdentityNodeTracks = true);

		Animation* Clone(const String& newName);

	protected:
		String m_name;
		float m_duration;
		InterpolationMode m_interpolationMode;
		RotationInterpolationMode m_rotationInterpolationMode;
		static InterpolationMode s_defaultInterpolationMode;
		static RotationInterpolationMode s_defaultRotationInterpolationMode;

		typedef std::vector<float> KeyFrameTimeList;
		mutable KeyFrameTimeList m_keyFrameTimes;
		/// Dirty flag indicate that keyframe time list need to rebuild
		mutable bool m_keyFrameTimesDirty;

		bool m_useBaseKeyFrame;
		float m_baseKeyFrameTime;
		String m_baseKeyFrameAnimationName;
		AnimationContainer* m_container;

		/// Node tracks, indexed by handle
		NodeTrackList m_nodeTrackList;

	protected:
		void BuildKeyFrameTimeList() const;

		void OptimizeNodeTracks(bool discardIdentityTracks = true);
	};
}
