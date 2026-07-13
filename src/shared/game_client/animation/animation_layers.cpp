// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "animation_layers.h"

#include <algorithm>
#include <cctype>

#include "log/default_log_levels.h"
#include "scene_graph/animation_state.h"
#include "scene_graph/bone.h"
#include "scene_graph/entity.h"
#include "scene_graph/skeleton_instance.h"

namespace mmo
{
	namespace
	{
		bool BelongsTo(const AnimationState* state, const AnimationStateSet* set)
		{
			return state != nullptr && set != nullptr && state->GetParent() == set;
		}
	}

	// ============================================================================
	// LocomotionLayer

	void LocomotionLayer::SetDesired(const WeightedAnimClip* clips, const uint32 count)
	{
		if (count == 0)
		{
			// Mirrors the legacy behavior of SetTargetAnimState(nullptr): keep playing
			// whatever is active instead of fading to the bind pose.
			return;
		}

		ASSERT(count <= 2);
		m_desiredCount = count;
		for (uint32 i = 0; i < count; ++i)
		{
			m_desired[i] = clips[i];
		}
	}

	float LocomotionLayer::DesiredWeightOf(const AnimationState& state) const
	{
		for (uint32 i = 0; i < m_desiredCount; ++i)
		{
			if (m_desired[i].state == &state)
			{
				return m_desired[i].weight;
			}
		}
		return 0.0f;
	}

	void LocomotionLayer::ApplyWeights(const float deltaTime, const float visibilityCap, const float fadeDuration)
	{
		// First-ever assignment snaps straight to the desired set so freshly spawned units
		// don't blend in from the bind pose.
		const bool snap = m_active.empty();

		// Make sure every desired clip has an active entry to ramp.
		for (uint32 i = 0; i < m_desiredCount; ++i)
		{
			AnimationState* state = m_desired[i].state;
			if (!state)
			{
				continue;
			}

			const bool known = std::any_of(m_active.begin(), m_active.end(),
				[state](const ActiveClip& clip) { return clip.state == state; });
			if (!known)
			{
				m_active.push_back(ActiveClip{ state, snap ? m_desired[i].weight : 0.0f });
			}
		}

		const float step = fadeDuration > 0.0f ? deltaTime / fadeDuration : 1.0f;

		for (auto it = m_active.begin(); it != m_active.end();)
		{
			const float target = DesiredWeightOf(*it->state);

			const float delta = std::clamp(target - it->weight, -step, step);
			it->weight = std::clamp(it->weight + delta, 0.0f, 1.0f);

			const float applied = std::min(it->weight, visibilityCap);
			it->state->SetWeight(applied);
			it->state->SetEnabled(applied > 0.0f);

			if (it->weight <= 0.0f && target <= 0.0f)
			{
				it->state->SetEnabled(false);
				it = m_active.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	void LocomotionLayer::AdvanceTime(const float deltaTime) const
	{
		for (const ActiveClip& clip : m_active)
		{
			if (clip.state->IsEnabled())
			{
				clip.state->AddTime(deltaTime);
			}
		}
	}

	void LocomotionLayer::Reset()
	{
		m_active.clear();
		m_desiredCount = 0;
	}

	void LocomotionLayer::ValidateAgainst(const AnimationStateSet* set)
	{
		m_active.erase(std::remove_if(m_active.begin(), m_active.end(),
			[set](const ActiveClip& clip) { return !BelongsTo(clip.state, set); }), m_active.end());

		uint32 kept = 0;
		for (uint32 i = 0; i < m_desiredCount; ++i)
		{
			if (BelongsTo(m_desired[i].state, set))
			{
				m_desired[kept++] = m_desired[i];
			}
		}
		m_desiredCount = kept;
	}

	// ============================================================================
	// ActionLayer

	bool ActionLayer::IsBusyFirstHalf() const
	{
		if (!m_current || m_current->HasEnded())
		{
			return false;
		}

		const float length = m_current->GetLength();
		const float progress = length > 0.0f ? m_current->GetTimePosition() / length : 1.0f;
		return progress < 0.5f;
	}

	void ActionLayer::SetPending(AnimationState& state)
	{
		m_pending = &state;
		m_pending->SetTimePosition(0.0f);
	}

	void ActionLayer::Start(AnimationState& state, const bool isPoseTransition)
	{
		m_pending = nullptr;

		if (m_current)
		{
			// Flush pending hit callbacks before evicting - the old animation's hit point passed.
			FlushHitCallbacks();
			m_current->SetEnabled(false);
			m_current->SetWeight(0.0f);
		}

		m_current = &state;
		m_currentIsPoseTransition = isPoseTransition;
		m_current->SetEnabled(true);
		m_current->SetWeight(1.0f);
		m_current->SetTimePosition(0.0f);
	}

	void ActionLayer::Cancel()
	{
		if (m_current)
		{
			m_current->SetEnabled(false);
			m_current->SetWeight(0.0f);
		}
		m_current = nullptr;
		m_pending = nullptr;
		m_currentIsPoseTransition = false;
		FlushHitCallbacks();
	}

	AnimationState* ActionLayer::Update(const float deltaTime, const float fadeDuration)
	{
		if (!m_current || !m_current->HasEnded())
		{
			return nullptr;
		}

		// Fade the finished one-shot back out; locomotion visibility rises in lockstep.
		const float step = fadeDuration > 0.0f ? deltaTime / fadeDuration : 1.0f;
		const float weight = m_current->GetWeight() - step;
		m_current->SetWeight(std::max(weight, 0.0f));

		if (weight > 0.0f)
		{
			return nullptr;
		}

		m_current->SetEnabled(false);
		m_current = nullptr;
		m_currentIsPoseTransition = false;

		// Flush any hit callbacks that never got a notify (animation had none).
		FlushHitCallbacks();

		AnimationState* pending = m_pending;
		m_pending = nullptr;
		return pending;
	}

	bool ActionLayer::IsPlaying() const
	{
		return m_current != nullptr && m_current->GetWeight() > 0.0f;
	}

	void ActionLayer::FastForwardCurrent() const
	{
		if (m_current && m_current->IsEnabled() && !m_current->HasEnded())
		{
			m_current->SetTimePosition(m_current->GetLength());
		}
	}

	float ActionLayer::LocomotionVisibility() const
	{
		if (!m_current)
		{
			return 1.0f;
		}

		if (!m_current->HasEnded())
		{
			return 0.0f;
		}

		return std::clamp(1.0f - m_current->GetWeight(), 0.0f, 1.0f);
	}

	void ActionLayer::QueueHitCallback(std::function<void()> callback)
	{
		if (!m_current || m_current->HasEnded())
		{
			callback();
			return;
		}

		m_hitCallbacks.push_back(std::move(callback));
	}

	void ActionLayer::FlushHitCallbacks()
	{
		if (m_hitCallbacks.empty())
		{
			return;
		}

		std::vector<std::function<void()>> callbacks;
		std::swap(callbacks, m_hitCallbacks);
		for (const auto& callback : callbacks)
		{
			callback();
		}
	}

	void ActionLayer::Reset()
	{
		m_current = nullptr;
		m_pending = nullptr;
		m_currentIsPoseTransition = false;
		m_hitCallbacks.clear();
	}

	void ActionLayer::ValidateAgainst(const AnimationStateSet* set)
	{
		if (m_current && !BelongsTo(m_current, set))
		{
			m_current = nullptr;
			m_currentIsPoseTransition = false;
		}
		if (m_pending && !BelongsTo(m_pending, set))
		{
			m_pending = nullptr;
		}
	}

	// ============================================================================
	// PoseLayer

	void PoseLayer::SetSpellLoop(AnimationState* state)
	{
		m_lockedLoop = state;
	}

	void PoseLayer::SetPoseLoop(AnimationState& state)
	{
		m_poseLoop = &state;
		m_lockedLoop = &state;
	}

	void PoseLayer::ClearPoseLoop()
	{
		if (m_lockedLoop == m_poseLoop)
		{
			m_lockedLoop = nullptr;
		}
		m_poseLoop = nullptr;
	}

	void PoseLayer::EvictSpellLoop()
	{
		if (m_lockedLoop != m_poseLoop)
		{
			m_lockedLoop = nullptr;
		}
	}

	void PoseLayer::Reset()
	{
		m_lockedLoop = nullptr;
		m_poseLoop = nullptr;
	}

	void PoseLayer::ValidateAgainst(const AnimationStateSet* set)
	{
		if (m_lockedLoop && !BelongsTo(m_lockedLoop, set))
		{
			m_lockedLoop = nullptr;
		}
		if (m_poseLoop && !BelongsTo(m_poseLoop, set))
		{
			m_poseLoop = nullptr;
		}
	}

	// ============================================================================
	// FaceOverlayLayer

	void FaceOverlayLayer::SetMoodClip(AnimationState* state, const Entity* entity)
	{
		// Disable the previous mood layer (if any).
		if (m_state)
		{
			m_state->SetEnabled(false);
			m_state->SetWeight(0.0f);
			m_state->DestroyBlendMask();
		}
		m_state = nullptr;

		if (!state || !entity)
		{
			return;
		}

		const std::shared_ptr<SkeletonInstance> skeleton = entity->GetSkeleton();
		if (!skeleton)
		{
			return;
		}

		// Restrict the mood clip to face bones (name convention: "face_" prefix,
		// case-insensitive) so it can layer over whatever the body is doing. Without any
		// face bones the mood is a silent no-op.
		const uint16 boneCount = skeleton->GetNumBones();
		state->CreateBlendMask(boneCount, 0.0f);

		bool anyFaceBone = false;
		for (uint16 i = 0; i < boneCount; ++i)
		{
			const Bone* bone = skeleton->GetBone(i);
			if (!bone)
			{
				continue;
			}

			const String& name = bone->GetName();
			constexpr const char* facePrefix = "face_";
			constexpr size_t facePrefixLen = 5;
			if (name.size() >= facePrefixLen &&
				std::equal(name.begin(), name.begin() + facePrefixLen, facePrefix,
					[](const char a, const char b) { return std::tolower(static_cast<unsigned char>(a)) == b; }))
			{
				state->SetBlendMaskEntry(bone->GetHandle(), 1.0f);
				anyFaceBone = true;
			}
		}

		if (!anyFaceBone)
		{
			state->DestroyBlendMask();
			return;
		}

		state->SetLoop(true);
		state->SetPlayRate(1.0f);
		state->SetTimePosition(0.0f);
		state->SetWeight(1.0f);
		state->SetEnabled(true);
		m_state = state;
	}

	void FaceOverlayLayer::AdvanceTime(const float deltaTime) const
	{
		if (m_state && m_state->IsEnabled())
		{
			m_state->AddTime(deltaTime);
		}
	}

	void FaceOverlayLayer::Reset()
	{
		m_state = nullptr;
	}

	void FaceOverlayLayer::ValidateAgainst(const AnimationStateSet* set)
	{
		if (m_state && !BelongsTo(m_state, set))
		{
			m_state = nullptr;
		}
	}
}
