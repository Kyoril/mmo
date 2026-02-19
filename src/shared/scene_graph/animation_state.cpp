#include "animation_state.h"
#include "animation.h"

#include <cmath>
#include <ranges>

namespace mmo
{
	AnimationState::AnimationState(String name, AnimationStateSet& parent, const float timePos, const float length, const float weight, const bool enabled)
		: m_blendMask(nullptr)
		, m_animationName(std::move(name))
		, m_parent(&parent)
		, m_timePos(timePos)
		, m_length(length)
		, m_weight(weight)
		, m_enabled(enabled)
		, m_loop(true)
	{
		m_parent->NotifyDirty();
	}

	AnimationState::AnimationState(AnimationStateSet& parent, const AnimationState& rhs)
		: m_blendMask(nullptr)
		, m_animationName(rhs.m_animationName)
		, m_parent(&parent)
		, m_timePos(rhs.m_timePos)
		, m_length(rhs.m_length)
		, m_weight(rhs.m_weight)
		, m_enabled(rhs.m_enabled)
		, m_loop(rhs.m_loop)
	{
		m_parent->NotifyDirty();
	}

	// Fixed SetTimePosition function - to be merged back
	void AnimationState::SetTimePosition(const float timePos)
	{
		if (timePos == m_timePos)
		{
			return;
		}

		const float oldTimePos = m_timePos;
		m_timePos = timePos;
		bool hasLooped = false;

		if (m_loop)
		{
			// Wrap
			m_timePos = std::fmod(m_timePos, m_length);
			if (m_timePos < 0)
			{
				m_timePos += m_length;
			}

			// Check if we wrapped around
			hasLooped = (oldTimePos > m_timePos && timePos > oldTimePos);
		}
		else
		{
			// Clamp
			if (m_timePos < 0)
			{
				m_timePos = 0;
			}
			else if (m_timePos >= m_length)
			{
				m_timePos = m_length;
			}
		}

		// Trigger animation notifies if we have an animation
		if (m_animation && m_enabled)
		{
			// If we looped, clear triggered notifies
			if (hasLooped)
			{
				m_triggeredNotifies.clear();
			}

			// Check each notify to see if we crossed its time
			const auto& notifies = m_animation->GetNotifies();
			for (size_t i = 0; i < notifies.size(); ++i)
			{
				const auto& notify = notifies[i];
				const float notifyTime = notify->GetTime();

				// Skip if already triggered
				if (std::find(m_triggeredNotifies.begin(), m_triggeredNotifies.end(), i) != m_triggeredNotifies.end())
				{
					continue;
				}

				// Check if we crossed this notify's time
				bool shouldTrigger = false;
				if (hasLooped)
				{
					// When looping, trigger if notify is between old and end, or between 0 and new
					shouldTrigger = (notifyTime >= oldTimePos && notifyTime < m_length) ||
						(notifyTime >= 0.0f && notifyTime <= m_timePos);
				}
				else
				{
					// Normal case: trigger if we passed the notify time
					shouldTrigger = (oldTimePos < notifyTime && m_timePos >= notifyTime) ||
						(oldTimePos > m_timePos && notifyTime <= m_timePos); // Handle reverse playback
				}

				if (shouldTrigger)
				{
					notifyTriggered(*notify, m_animationName, *this);
					m_triggeredNotifies.push_back(i);
				}
			}
		}

		m_lastTimePos = m_timePos;

		if (m_enabled)
		{
			m_parent->NotifyDirty();
		}
	}

	void AnimationState::SetWeight(const float weight)
	{
		if (m_weight == weight)
		{
			return;
		}

		m_weight = weight;
		if (m_weight < 0.0f) m_weight = 0.0f;
		if (m_weight > 1.0f) m_weight = 1.0f;

		if (m_enabled)
		{
			m_parent->NotifyDirty();
		}
	}

	void AnimationState::AddTime(const float offset)
	{
		SetTimePosition(m_timePos + (offset * m_playRate));
	}

	void AnimationState::SetEnabled(const bool enabled)
	{
		if (m_enabled == enabled)
		{
			return;
		}

		m_enabled = enabled;
		m_parent->NotifyAnimationStateEnabled(this, enabled);
	}

	bool AnimationState::operator==(const AnimationState& rhs) const
	{
		return m_animationName == rhs.m_animationName &&
			m_enabled == rhs.m_enabled &&
			m_timePos == rhs.m_timePos &&
			m_weight == rhs.m_weight &&
			m_length == rhs.m_length &&
			m_loop == rhs.m_loop;
	}

	bool AnimationState::operator!=(const AnimationState& rhs) const
	{
		return !(*this == rhs);
	}

	void AnimationState::CopyStateFrom(const AnimationState& animState)
	{
		m_timePos = animState.m_timePos;
		m_length = animState.m_length;
		m_weight = animState.m_weight;
		m_enabled = animState.m_enabled;
		m_loop = animState.m_loop;
		m_parent->NotifyDirty();
	}

	void AnimationState::CreateBlendMask(size_t blendMaskSizeHint, float initialWeight)
	{
		if (!m_blendMask)
		{
			if (initialWeight >= 0)
			{
				m_blendMask = std::make_unique<BoneBlendMask>(blendMaskSizeHint, initialWeight);
			}
			else
			{
				m_blendMask = std::make_unique<BoneBlendMask>(blendMaskSizeHint);
			}
		}
	}

	void AnimationState::DestroyBlendMask()
	{
		m_blendMask.reset();
	}

	void AnimationState::SetBlendMaskData(const float* blendMaskData)
	{
		ASSERT(m_blendMask && "No BlendMask set!");

		if (!blendMaskData)
		{
			DestroyBlendMask();
			return;
		}

		// dangerous memcpy
		memcpy(m_blendMask->data(), blendMaskData, sizeof(float) * m_blendMask->size());
		if (m_enabled)
		{
			m_parent->NotifyDirty();
		}
	}

	void AnimationState::SetBlendMask(const BoneBlendMask* blendMask)
	{
		if (!m_blendMask || m_blendMask->size() != blendMask->size())
		{
			CreateBlendMask(blendMask->size(), false);
		}

		SetBlendMaskData(blendMask->data());
	}

	void AnimationState::SetBlendMaskEntry(const uint16 boneHandle, const float weight) const
	{
		ASSERT(m_blendMask && m_blendMask->size() > boneHandle);

		(*m_blendMask)[boneHandle] = weight;
		if (m_enabled)
		{
			m_parent->NotifyDirty();
		}
	}

	AnimationStateSet::AnimationStateSet()
		: m_dirtyFrameNumber(std::numeric_limits<unsigned long>::max())
	{
	}

	AnimationStateSet::AnimationStateSet(const AnimationStateSet& rhs)
		: m_dirtyFrameNumber(std::numeric_limits<unsigned long>::max())
	{
		for (const auto& animationState : rhs.m_animationStates | std::views::values)
		{
			m_animationStates[animationState->GetAnimationName()] = std::make_unique<AnimationState>(*this, *animationState);
		}

		// Clone enabled animation state list
		for (const auto src : rhs.m_enabledAnimationStates)
		{
			m_enabledAnimationStates.push_back(GetAnimationState(src->GetAnimationName()));
		}
	}

	AnimationStateSet::~AnimationStateSet()
	{
		RemoveAllAnimationStates();
	}

	AnimationState* AnimationStateSet::CreateAnimationState(const String& name, float timePos, float length, float weight, bool enabled)
	{
		const auto i = m_animationStates.find(name);
		ASSERT(i == m_animationStates.end() && "AnimationState with this name already exists!");

		return (m_animationStates[name] = std::make_unique<AnimationState>(name, *this, timePos, length, weight, enabled)).get();
	}

	AnimationState* AnimationStateSet::GetAnimationState(const String& name) const
	{
		const auto i = m_animationStates.find(name);
		return i != m_animationStates.end() ? i->second.get() : nullptr;
	}

	bool AnimationStateSet::HasAnimationState(const String& name) const
	{
		return m_animationStates.contains(name);
	}

	void AnimationStateSet::RemoveAnimationState(const String& name)
	{
		const auto i = m_animationStates.find(name);
		ASSERT(i != m_animationStates.end() && "AnimationState with this name does not exist!");

		if (i != m_animationStates.end())
		{
			m_enabledAnimationStates.remove(i->second.get());
			m_animationStates.erase(i);
		}
	}

	void AnimationStateSet::RemoveAllAnimationStates()
	{
		m_animationStates.clear();
		m_enabledAnimationStates.clear();
	}

	void AnimationStateSet::CopyMatchingState(AnimationStateSet* target) const
	{
		for (auto& [name, state] : target->m_animationStates)
		{
			auto otherStateIt = m_animationStates.find(name);
			ASSERT(otherStateIt != m_animationStates.end() && "AnimationState with this name does not exist!");

			state->CopyStateFrom(*(otherStateIt->second));
		}

		// Copy matching enabled animation state list
		target->m_enabledAnimationStates.clear();

		for (const auto src : m_enabledAnimationStates)
		{
			if (auto targetIt = target->m_animationStates.find(src->GetAnimationName()); targetIt != target->m_animationStates.end())
			{
				target->m_enabledAnimationStates.push_back(targetIt->second.get());
			}
		}

		target->m_dirtyFrameNumber = m_dirtyFrameNumber;
	}

	void AnimationStateSet::NotifyDirty()
	{
		++m_dirtyFrameNumber;
	}

	void AnimationStateSet::NotifyAnimationStateEnabled(AnimationState* target, bool enabled)
	{
		// Remove from enabled animation state list first
		m_enabledAnimationStates.remove(target);

		// Add to enabled animation state list if need
		if (enabled)
		{
			m_enabledAnimationStates.push_back(target);
		}

		// Set the dirty frame number
		NotifyDirty();
	}
}


