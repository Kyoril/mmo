// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "animation_controller.h"

#include <random>

#include "client_data/project.h"
#include "game/movement_info.h"
#include "log/default_log_levels.h"
#include "scene_graph/animation_state.h"
#include "scene_graph/entity.h"

namespace mmo
{
	AnimationController::AnimationController(Entity*& entity, const proto_client::Project& project)
		: m_entity(entity)
		, m_project(project)
	{
	}

	void AnimationController::SetProfile(const proto_client::AnimationProfileEntry* profile)
	{
		if (m_profile == profile)
		{
			return;
		}

		m_profile = profile;
		m_bindingsDirty = true;
	}

	void AnimationController::SetProfileId(const uint32 profileId)
	{
		const proto_client::AnimationProfileEntry* profile = nullptr;
		if (profileId != 0)
		{
			profile = m_project.animationProfiles.getById(profileId);
			if (!profile)
			{
				WLOG("Animation profile " << profileId << " not found, falling back to default clip names");
			}
		}

		SetProfile(profile);
	}

	void AnimationController::NotifyMeshChanged()
	{
		m_locomotion.Reset();
		m_action.Reset();
		m_pose.Reset();
		m_face.Reset();
		m_bindings.Invalidate();
		m_lootPoseActive = false;
		m_bindingsDirty = true;
	}

	void AnimationController::EnsureBindings()
	{
		if (!m_bindingsDirty && !m_bindings.IsStale(m_entity))
		{
			return;
		}

		// A stale table means the mesh (and its animation state set) changed underneath us:
		// drop every clip pointer the layers still hold before they touch freed memory.
		if (m_bindings.IsStale(m_entity))
		{
			ValidateLayers();
		}

		m_bindings.Rebuild(m_entity, m_profile, m_conditionMask, m_runtimeBinds);
		m_bindingsDirty = false;
	}

	void AnimationController::ValidateLayers()
	{
		const AnimationStateSet* currentSet = m_entity ? m_entity->GetAllAnimationStates() : nullptr;
		m_locomotion.ValidateAgainst(currentSet);
		m_action.ValidateAgainst(currentSet);
		m_pose.ValidateAgainst(currentSet);
		m_face.ValidateAgainst(currentSet);
	}

	uint32 AnimationController::ComputeConditionMask(const AnimationContext& ctx)
	{
		uint32 mask = anim_condition_flags::None;

		if (ctx.walkMode)
		{
			mask |= anim_condition_flags::Walk;
		}
		if (ctx.swimming)
		{
			mask |= anim_condition_flags::Swim;
		}
		if (ctx.stealthed)
		{
			mask |= anim_condition_flags::Stealth;
		}
		if (ctx.weaponDrawn)
		{
			mask |= anim_condition_flags::Combat;
			switch (ctx.weaponClass)
			{
			case anim_weapon_class::OneHanded:
				mask |= anim_condition_flags::Combat1H;
				break;
			case anim_weapon_class::TwoHanded:
				mask |= anim_condition_flags::Combat2H;
				break;
			default:
				mask |= anim_condition_flags::CombatUnarmed;
				break;
			}
		}

		return mask;
	}

	float AnimationController::MovementAngleFromFlags(const uint32 movementFlags)
	{
		const bool forward = (movementFlags & movement_flags::Forward) != 0;
		const bool backward = (movementFlags & movement_flags::Backward) != 0 && !forward;
		const bool strafeLeft = (movementFlags & movement_flags::StrafeLeft) != 0 &&
			(movementFlags & movement_flags::StrafeRight) == 0;
		const bool strafeRight = (movementFlags & movement_flags::StrafeRight) != 0 &&
			(movementFlags & movement_flags::StrafeLeft) == 0;

		if (forward)
		{
			return strafeRight ? 45.0f : (strafeLeft ? 315.0f : 0.0f);
		}
		if (backward)
		{
			return strafeRight ? 135.0f : (strafeLeft ? 225.0f : 180.0f);
		}
		if (strafeRight)
		{
			return 90.0f;
		}
		if (strafeLeft)
		{
			return 270.0f;
		}

		return 0.0f;
	}

	void AnimationController::EvaluateLocomotion(const AnimationContext& ctx)
	{
		WeightedAnimClip clips[2];
		uint32 clipCount = 0;

		const auto single = [&clips, &clipCount](AnimationState* state)
		{
			if (state)
			{
				clips[0] = WeightedAnimClip{ state, 1.0f };
				clipCount = 1;
			}
		};

		if (ctx.dead)
		{
			single(m_bindings.Get(proto_client::ANIM_SLOT_DEATH));
			m_idleSeconds = 0.0f;
		}
		else if (ctx.looting)
		{
			// Ahead of the pose lock so looting while seated kneels and returns to sitting
			// afterwards. The clip does not loop, so AnimationState clamps it at its length
			// and it holds its last frame for as long as the loot window stays open.
			AnimationState* lootClip = m_bindings.Get(proto_client::ANIM_SLOT_LOOT);
			if (lootClip)
			{
				if (!m_lootPoseActive)
				{
					lootClip->SetLoop(false);
					lootClip->SetTimePosition(0.0f);

					// Finishing an Open cast on a chest fires the 1.37s UseEnd one-shot at the
					// same instant the loot window opens. Without this the character stands up
					// out of the chest and only then kneels.
					m_action.FastForwardCurrent();
					m_lootPoseActive = true;
				}

				single(lootClip);
			}

			m_idleSeconds = 0.0f;
		}
		else if (AnimationState* lock = m_pose.GetLock())
		{
			single(lock);
			m_idleSeconds = 0.0f;
		}
		else if (ctx.swimming)
		{
			const bool horizontallyMoving = (ctx.movementFlags &
				(movement_flags::Forward | movement_flags::Backward |
					movement_flags::StrafeLeft | movement_flags::StrafeRight)) != 0;

			if (horizontallyMoving)
			{
				clipCount = m_bindings.GetMovementBlendSpace().Evaluate(
					MovementAngleFromFlags(ctx.movementFlags), clips);
				if (clipCount == 0)
				{
					single(m_bindings.Get(proto_client::ANIM_SLOT_MOVE_FORWARD));
				}
			}
			else
			{
				// Treading water / ascending in place: the swim override set binds the idle
				// slot to the swim-idle clip.
				single(m_bindings.Get(proto_client::ANIM_SLOT_IDLE));
			}
			m_idleSeconds = 0.0f;
		}
		else if (ctx.airborne)
		{
			AnimationState* jumpStart = m_bindings.Get(proto_client::ANIM_SLOT_JUMP_START);
			if (ctx.verticalVelocity > 0.0f && jumpStart && !jumpStart->HasEnded())
			{
				single(jumpStart);
			}
			else
			{
				single(m_bindings.Get(proto_client::ANIM_SLOT_FALL));
			}
			m_idleSeconds = 0.0f;
		}
		else if (ctx.moving || ctx.pathMoving)
		{
			const float angle = ctx.pathMoving ? 0.0f : MovementAngleFromFlags(ctx.movementFlags);
			clipCount = m_bindings.GetMovementBlendSpace().Evaluate(angle, clips);
			if (clipCount == 0)
			{
				single(m_bindings.Get(proto_client::ANIM_SLOT_MOVE_FORWARD));
			}
			m_idleSeconds = 0.0f;
		}
		else if (ctx.weaponDrawn)
		{
			single(m_bindings.Get(proto_client::ANIM_SLOT_COMBAT_IDLE));
			m_idleSeconds = 0.0f;
		}
		else
		{
			// After standing still for a while, ease into the unit's selected special idle
			// pose (if one is selected and its clip exists on the current mesh).
			constexpr float specialIdleDelaySeconds = 10.0f;

			m_idleSeconds += ctx.deltaTime;

			AnimationState* idle = m_bindings.Get(proto_client::ANIM_SLOT_IDLE);
			if (m_idleSeconds >= specialIdleDelaySeconds && !m_action.IsActive())
			{
				if (AnimationState* specialIdle = m_bindings.Get(proto_client::ANIM_SLOT_SPECIAL_IDLE))
				{
					idle = specialIdle;
				}
			}
			single(idle);
		}

		if (!ctx.looting)
		{
			m_lootPoseActive = false;
		}

		m_locomotion.SetDesired(clips, clipCount);
	}

	void AnimationController::Update(const AnimationContext& ctx)
	{
		if (!m_entity)
		{
			NotifyMeshChanged();
			return;
		}

		const uint32 mask = ComputeConditionMask(ctx);
		if (mask != m_conditionMask)
		{
			m_conditionMask = mask;
			m_bindingsDirty = true;
		}

		EnsureBindings();

		// A pose enter/exit transition must never mask movement: fast-forward it as soon as
		// the unit starts moving (remote stand-state and movement packets can interleave so a
		// movement start lands mid-transition). The action layer then blends it out.
		const bool changingPosition = (ctx.movementFlags & movement_flags::PositionChanging) != 0;
		if (m_action.CurrentIsPoseTransition() && (ctx.moving || ctx.pathMoving || changingPosition))
		{
			m_action.FastForwardCurrent();
		}

		if (ctx.dead)
		{
			// Death overrides everything: finish any one-shot instantly and release all locks.
			m_action.FastForwardCurrent();
			m_action.ClearPoseTransitionFlag();
			m_pose.Reset();
		}

		EvaluateLocomotion(ctx);

		const float fadeDuration = m_bindings.GetFadeDuration();

		if (AnimationState* pending = m_action.Update(ctx.deltaTime, fadeDuration))
		{
			PlayActionInternal(pending, false, false);
		}

		m_locomotion.ApplyWeights(ctx.deltaTime, m_action.LocomotionVisibility(), fadeDuration);
	}

	void AnimationController::AdvanceClipTimes(const float deltaTime)
	{
		if (!m_entity)
		{
			return;
		}

		// Runs after Update() so newly enabled clips start at their reset positions.
		m_locomotion.AdvanceTime(deltaTime);
		if (AnimationState* action = m_action.GetCurrent(); action && action->IsEnabled())
		{
			action->AddTime(deltaTime);
		}
		m_face.AdvanceTime(deltaTime);
	}

	bool AnimationController::PlayActionInternal(AnimationState* state, const bool suppressIfBusy, const bool isPoseTransition)
	{
		if (!state || !m_entity)
		{
			return false;
		}

		// Reject stale pointers from a previous mesh.
		const AnimationStateSet* currentSet = m_entity->GetAllAnimationStates();
		if (!currentSet || state->GetParent() != currentSet)
		{
			return false;
		}

		ValidateLayers();

		if (state->IsLoop())
		{
			WLOG("One shot animation has loop flag set to true, not playing!");
			return false;
		}

		// If a one-shot is currently playing and still in its first half, decide how to
		// handle the new request based on the caller's intent rather than always hard-cutting.
		if (m_action.IsBusyFirstHalf())
		{
			if (suppressIfBusy)
			{
				// Off-hand swings: drop silently - the main-hand animation is still fresh.
				return false;
			}

			// Non-off-hand (instant abilities, etc.): queue as pending, replacing any prior entry.
			m_action.SetPending(*state);
			return false;
		}

		// One-shot animations evict a locked spell loop; the pose lock survives (pose
		// transitions and flinches play in front of the pose loop and blend back into it).
		m_pose.EvictSpellLoop();

		m_action.Start(*state, isPoseTransition);
		return true;
	}

	bool AnimationController::PlayAction(AnimationState* state, const bool suppressIfBusy)
	{
		return PlayActionInternal(state, suppressIfBusy, false);
	}

	bool AnimationController::PlayPoseTransition(AnimationState* state)
	{
		return PlayActionInternal(state, false, true);
	}

	void AnimationController::CancelAction()
	{
		m_action.Cancel();
	}

	void AnimationController::QueueActionHitCallback(std::function<void()> callback)
	{
		ValidateLayers();
		m_action.QueueHitCallback(std::move(callback));
	}

	void AnimationController::NotifyActionHit()
	{
		m_action.FlushHitCallbacks();
	}

	bool AnimationController::PlayAttackSwing(const bool offhand)
	{
		EnsureBindings();

		// Off-hand swings use the dedicated off-hand list, falling back to the main-hand
		// list when none are available.
		const std::vector<AnimationState*>* candidates = nullptr;
		if (offhand && !m_bindings.GetOffHandAttackStates().empty())
		{
			candidates = &m_bindings.GetOffHandAttackStates();
		}
		else if (!m_bindings.GetMainHandAttackStates().empty())
		{
			candidates = &m_bindings.GetMainHandAttackStates();
		}

		// Fall back to the unarmed attack animation when no weapon animation is available.
		if (!candidates)
		{
			return PlayActionInternal(m_bindings.Get(proto_client::ANIM_SLOT_ATTACK), offhand, false);
		}

		// Pick one of the weapon attack animations at random.
		AnimationState* attackState = candidates->front();
		if (candidates->size() > 1)
		{
			static std::random_device rd;
			static std::mt19937 gen(rd());
			std::uniform_int_distribution<size_t> dis(0, candidates->size() - 1);
			attackState = (*candidates)[dis(gen)];
		}

		// Off-hand swings are suppressed when the main-hand animation is still fresh to avoid
		// the jarring visual of hard-cutting a recently-started clip.
		return PlayActionInternal(attackState, offhand, false);
	}

	void AnimationController::PlayHit()
	{
		EnsureBindings();
		PlayActionInternal(m_bindings.Get(proto_client::ANIM_SLOT_HIT), false, false);
	}

	void AnimationController::OnLanded()
	{
		EnsureBindings();

		if (AnimationState* land = m_bindings.Get(proto_client::ANIM_SLOT_LAND))
		{
			land->SetTimePosition(0.0f);
			PlayActionInternal(land, false, false);
		}

		// Re-arm the jump start clip for the next jump.
		if (AnimationState* jumpStart = m_bindings.Get(proto_client::ANIM_SLOT_JUMP_START))
		{
			jumpStart->SetTimePosition(0.0f);
		}
	}

	void AnimationController::SetSpellLoopAnimation(AnimationState* state)
	{
		if (state && m_entity)
		{
			const AnimationStateSet* currentSet = m_entity->GetAllAnimationStates();
			if (!currentSet || state->GetParent() != currentSet)
			{
				return;
			}
		}

		m_pose.SetSpellLoop(state);
	}

	void AnimationController::SetPoseLoop(AnimationState& state)
	{
		m_pose.SetPoseLoop(state);
	}

	void AnimationController::ClearPoseLoop()
	{
		m_pose.ClearPoseLoop();
	}

	void AnimationController::SetMoodClip(AnimationState* state)
	{
		m_face.SetMoodClip(state, m_entity);
	}

	void AnimationController::SetCombatReadyClip(const String& clipName)
	{
		if (m_runtimeBinds.combatReadyClip == clipName)
		{
			return;
		}

		m_runtimeBinds.combatReadyClip = clipName;
		m_bindingsDirty = true;
	}

	void AnimationController::SetAttackClips(std::vector<String> clipNames)
	{
		if (m_runtimeBinds.mainHandAttackClips == clipNames)
		{
			return;
		}

		m_runtimeBinds.mainHandAttackClips = std::move(clipNames);
		m_bindingsDirty = true;
	}

	void AnimationController::SetOffhandAttackClips(std::vector<String> clipNames)
	{
		if (m_runtimeBinds.offHandAttackClips == clipNames)
		{
			return;
		}

		m_runtimeBinds.offHandAttackClips = std::move(clipNames);
		m_bindingsDirty = true;
	}

	void AnimationController::SetSpecialIdleClip(const String& clipName)
	{
		if (m_runtimeBinds.specialIdleClip == clipName)
		{
			return;
		}

		m_runtimeBinds.specialIdleClip = clipName;
		m_bindingsDirty = true;
	}
}
