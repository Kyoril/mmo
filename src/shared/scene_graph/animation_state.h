#pragma once

#include "base/typedefs.h"
#include "base/macros.h"

#include <vector>
#include <map>
#include <list>
#include <memory>

namespace mmo
{
	class AnimationStateSet;

	class AnimationState
	{
	public:

		typedef std::vector<float> BoneBlendMask;

	public:
		AnimationState(String name, AnimationStateSet& parent, float timePos, float length, float weight = 1.0f, bool enabled = false);
        AnimationState(AnimationStateSet& parent, const AnimationState& rhs);

	public:
        [[nodiscard]] const String& GetAnimationName() const { return m_animationName; }

        [[nodiscard]] float GetTimePosition() const { return m_timePos; }

		void SetTimePosition(float timePos);

        [[nodiscard]] float GetLength() const { return m_length; }

        void SetLength(const float length) { m_length = length; }

        [[nodiscard]] float GetWeight() const { return m_weight; }

        void SetWeight(float weight);

        void AddTime(float offset);

		[[nodiscard]] bool HasEnded() const { return m_timePos >= m_length && !m_loop; }

        [[nodiscard]] bool IsEnabled() const { return m_enabled; }

        void SetEnabled(bool enabled);

        bool operator==(const AnimationState& rhs) const;
        bool operator!=(const AnimationState& rhs) const;

        void SetLoop(const bool loop) { m_loop = loop; }

        [[nodiscard]] bool IsLoop() const { return m_loop; }

        void CopyStateFrom(const AnimationState& animState);

        [[nodiscard]] AnimationStateSet* GetParent() const { return m_parent; }

        void CreateBlendMask(size_t blendMaskSizeHint, float initialWeight = 1.0f);

        void DestroyBlendMask();

        void SetBlendMaskData(const float* blendMaskData);

        void SetBlendMask(const BoneBlendMask* blendMask);

        [[nodiscard]] const BoneBlendMask* GetBlendMask() const { return m_blendMask.get(); }

        [[nodiscard]] bool HasBlendMask() const { return m_blendMask != nullptr; }

        /// Set the weight for the bone identified by the given handle
        void SetBlendMaskEntry(uint16 boneHandle, float weight) const;

        /// Get the weight for the bone identified by the given handle
        [[nodiscard]] float GetBlendMaskEntry(const size_t boneHandle) const
        {
            ASSERT(m_blendMask && m_blendMask->size() > boneHandle);
            return (*m_blendMask)[boneHandle];
        }

	protected:
		/// The blend mask (containing per bone weights)
		std::unique_ptr<BoneBlendMask> m_blendMask;

		String m_animationName;
		AnimationStateSet* m_parent;
		float m_timePos;
		float m_length;
		float m_weight;
		bool m_enabled;
		bool m_loop;
	};

    // A map of animation states
    typedef std::map<String, std::unique_ptr<AnimationState>> AnimationStateMap;
    // A list of enabled animation states
    typedef std::list<AnimationState*> EnabledAnimationStateList;


	class AnimationStateSet
	{
	public:
        AnimationStateSet();

        AnimationStateSet(const AnimationStateSet& rhs);

        ~AnimationStateSet();

	public:
        AnimationState* CreateAnimationState(const String& name, float timePos, float length, float weight = 1.0, bool enabled = false);

        [[nodiscard]] AnimationState* GetAnimationState(const String& name) const;

        [[nodiscard]] bool HasAnimationState(const String& name) const;

        void RemoveAnimationState(const String& name);

        void RemoveAllAnimationStates();

        void CopyMatchingState(AnimationStateSet* target) const;

        void NotifyDirty();

        [[nodiscard]] uint64 GetDirtyFrameNumber() const { return m_dirtyFrameNumber; }

        void NotifyAnimationStateEnabled(AnimationState* target, bool enabled);

        [[nodiscard]] bool HasEnabledAnimationState() const { return !m_enabledAnimationStates.empty(); }

        [[nodiscard]] const EnabledAnimationStateList& GetEnabledAnimationStates() const { return m_enabledAnimationStates; }

    protected:
        unsigned long m_dirtyFrameNumber;
        AnimationStateMap m_animationStates;
        EnabledAnimationStateList m_enabledAnimationStates;
	};
}
