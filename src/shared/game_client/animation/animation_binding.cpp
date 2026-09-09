// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "animation_binding.h"

#include <iterator>

#include "scene_graph/animation_state.h"
#include "scene_graph/entity.h"

namespace mmo
{
	namespace
	{
		using proto_client::AnimationSlot;

		/// A built-in slot binding: an ordered list of conventional clip names to try. The
		/// candidate lists reproduce the legacy hardcoded fallback behavior (e.g. a missing
		/// "WalkLeft" falls back to "Walk", never to "RunLeft").
		struct BuiltinBinding
		{
			AnimationSlot slot;
			const char* candidates[3];
		};

		/// A built-in blend space sample used to synthesize the directional movement blend
		/// space from conventional clip names.
		struct BuiltinSample
		{
			float angle;
			const char* clip;
		};

		constexpr BuiltinBinding builtinBase[] = {
			{ proto_client::ANIM_SLOT_IDLE, { "Idle" } },
			{ proto_client::ANIM_SLOT_COMBAT_IDLE, { "UnarmedReady", "Idle" } },
			{ proto_client::ANIM_SLOT_MOVE_FORWARD, { "Run" } },
			{ proto_client::ANIM_SLOT_MOVE_BACKWARD, { "RunBack", "Run" } },
			{ proto_client::ANIM_SLOT_MOVE_LEFT, { "RunLeft", "Run" } },
			{ proto_client::ANIM_SLOT_MOVE_RIGHT, { "RunRight", "Run" } },
			{ proto_client::ANIM_SLOT_MOVE_FORWARD_LEFT, { "RunForwardLeft", "Run" } },
			{ proto_client::ANIM_SLOT_MOVE_FORWARD_RIGHT, { "RunForwardRight", "Run" } },
			{ proto_client::ANIM_SLOT_MOVE_BACKWARD_LEFT, { "RunBack", "Run" } },
			{ proto_client::ANIM_SLOT_MOVE_BACKWARD_RIGHT, { "RunBack", "Run" } },
			{ proto_client::ANIM_SLOT_JUMP_START, { "JumpStart" } },
			{ proto_client::ANIM_SLOT_FALL, { "Falling" } },
			{ proto_client::ANIM_SLOT_LAND, { "Land" } },
			{ proto_client::ANIM_SLOT_DEATH, { "Death" } },
			{ proto_client::ANIM_SLOT_HIT, { "Hit" } },
			{ proto_client::ANIM_SLOT_ATTACK, { "UnarmedAttack01" } },
			// No fallback candidate on purpose: a rig without a "Loot" clip must play its
			// normal idle rather than freeze in some unrelated pose.
			{ proto_client::ANIM_SLOT_LOOT, { "Loot" } },
		};

		constexpr BuiltinBinding builtinWalk[] = {
			{ proto_client::ANIM_SLOT_MOVE_FORWARD, { "Walk" } },
			{ proto_client::ANIM_SLOT_MOVE_BACKWARD, { "Walk" } },
			{ proto_client::ANIM_SLOT_MOVE_LEFT, { "WalkLeft", "Walk" } },
			{ proto_client::ANIM_SLOT_MOVE_RIGHT, { "WalkRight", "Walk" } },
			{ proto_client::ANIM_SLOT_MOVE_FORWARD_LEFT, { "WalkForwardLeft", "Walk" } },
			{ proto_client::ANIM_SLOT_MOVE_FORWARD_RIGHT, { "WalkForwardRight", "Walk" } },
			{ proto_client::ANIM_SLOT_MOVE_BACKWARD_LEFT, { "Walk" } },
			{ proto_client::ANIM_SLOT_MOVE_BACKWARD_RIGHT, { "Walk" } },
		};

		constexpr BuiltinBinding builtinSwim[] = {
			{ proto_client::ANIM_SLOT_IDLE, { "SwimIdle", "Swim" } },
			{ proto_client::ANIM_SLOT_MOVE_FORWARD, { "Swim", "SwimIdle" } },
			{ proto_client::ANIM_SLOT_MOVE_BACKWARD, { "SwimBackward", "Swim", "SwimIdle" } },
			{ proto_client::ANIM_SLOT_MOVE_LEFT, { "SwimLeft", "Swim", "SwimIdle" } },
			{ proto_client::ANIM_SLOT_MOVE_RIGHT, { "SwimRight", "Swim", "SwimIdle" } },
			{ proto_client::ANIM_SLOT_MOVE_FORWARD_LEFT, { "Swim", "SwimIdle" } },
			{ proto_client::ANIM_SLOT_MOVE_FORWARD_RIGHT, { "Swim", "SwimIdle" } },
			{ proto_client::ANIM_SLOT_MOVE_BACKWARD_LEFT, { "SwimBackward", "Swim", "SwimIdle" } },
			{ proto_client::ANIM_SLOT_MOVE_BACKWARD_RIGHT, { "SwimBackward", "Swim", "SwimIdle" } },
			{ proto_client::ANIM_SLOT_DEATH, { "SwimDeath" } },
		};

		constexpr BuiltinSample builtinBaseSamples[] = {
			{ 0.0f, "Run" }, { 45.0f, "RunForwardRight" }, { 90.0f, "RunRight" },
			{ 180.0f, "RunBack" }, { 270.0f, "RunLeft" }, { 315.0f, "RunForwardLeft" }
		};

		constexpr BuiltinSample builtinWalkSamples[] = {
			{ 0.0f, "Walk" }, { 45.0f, "WalkForwardRight" }, { 90.0f, "WalkRight" },
			{ 180.0f, "Walk" }, { 270.0f, "WalkLeft" }, { 315.0f, "WalkForwardLeft" }
		};

		constexpr BuiltinSample builtinSwimSamples[] = {
			{ 0.0f, "Swim" }, { 90.0f, "SwimRight" }, { 180.0f, "SwimBackward" }, { 270.0f, "SwimLeft" }
		};

		/// One entry in the ordered resolution stack: either a proto clip set or a built-in table.
		struct SetRef
		{
			const proto_client::AnimationClipSet* proto{nullptr};
			const BuiltinBinding* builtin{nullptr};
			size_t builtinCount{0};
			const BuiltinSample* samples{nullptr};
			size_t sampleCount{0};
		};

		bool IsMovementSlot(const uint32 slot)
		{
			return slot >= proto_client::ANIM_SLOT_MOVE_FORWARD && slot <= proto_client::ANIM_SLOT_MOVE_BACKWARD_RIGHT;
		}

		bool IsLoopingSlot(const uint32 slot)
		{
			switch (slot)
			{
			case proto_client::ANIM_SLOT_JUMP_START:
			case proto_client::ANIM_SLOT_LAND:
			case proto_client::ANIM_SLOT_DEATH:
			case proto_client::ANIM_SLOT_HIT:
			case proto_client::ANIM_SLOT_ATTACK:
			// The Loot clip is a one-shot kneel whose last frame is held for the duration of
			// the loot window; a binding rebuild (weapon draw, combat, stealth, water) must
			// never re-enable looping on it, or the pose starts cycling instead of holding.
			case proto_client::ANIM_SLOT_LOOT:
				return false;
			default:
				return true;
			}
		}

		float SlotAngle(const uint32 slot)
		{
			switch (slot)
			{
			case proto_client::ANIM_SLOT_MOVE_FORWARD:			return 0.0f;
			case proto_client::ANIM_SLOT_MOVE_FORWARD_RIGHT:	return 45.0f;
			case proto_client::ANIM_SLOT_MOVE_RIGHT:			return 90.0f;
			case proto_client::ANIM_SLOT_MOVE_BACKWARD_RIGHT:	return 135.0f;
			case proto_client::ANIM_SLOT_MOVE_BACKWARD:			return 180.0f;
			case proto_client::ANIM_SLOT_MOVE_BACKWARD_LEFT:	return 225.0f;
			case proto_client::ANIM_SLOT_MOVE_LEFT:				return 270.0f;
			case proto_client::ANIM_SLOT_MOVE_FORWARD_LEFT:		return 315.0f;
			default:											return 0.0f;
			}
		}

		const proto_client::AnimationSlotBinding* FindProtoBinding(const proto_client::AnimationClipSet& set, const uint32 slot)
		{
			for (const auto& binding : set.bindings())
			{
				if (binding.slot() == slot)
				{
					return &binding;
				}
			}
			return nullptr;
		}

		bool ProtoSetDefinesMovement(const proto_client::AnimationClipSet& set)
		{
			if (set.has_movement() && set.movement().clips_size() > 0)
			{
				return true;
			}

			for (const auto& binding : set.bindings())
			{
				if (IsMovementSlot(binding.slot()))
				{
					return true;
				}
			}
			return false;
		}
	}

	void SlotBindingTable::Rebuild(Entity* entity, const proto_client::AnimationProfileEntry* profile,
		const uint32 conditionMask, const AnimationRuntimeBinds& runtimeBinds)
	{
		m_bindings.fill(nullptr);
		m_movementBlendSpace.Clear();
		m_mainHandAttackStates.clear();
		m_offHandAttackStates.clear();
		m_conditionMask = conditionMask;
		m_fadeDuration = profile ? profile->fade_duration() : 0.25f;
		m_sourceSet = entity ? entity->GetAllAnimationStates() : nullptr;

		if (!entity || !m_sourceSet)
		{
			return;
		}

		// Build the ordered resolution stack, condition-major: a condition-specific set (proto
		// override, then built-in equivalent) always beats a more generic one, regardless of
		// whether it comes from the profile or the built-in defaults.
		std::vector<SetRef> stack;
		stack.reserve(10);

		const auto pushProtoOverride = [&](const proto_client::AnimationCondition condition)
		{
			if (!profile)
			{
				return;
			}

			for (const auto& overrideSet : profile->overrides())
			{
				if (overrideSet.condition() == static_cast<uint32>(condition))
				{
					SetRef ref;
					ref.proto = &overrideSet.clips();
					stack.push_back(ref);
					return;
				}
			}
		};

		if (conditionMask & anim_condition_flags::Stealth)
		{
			pushProtoOverride(proto_client::ANIM_COND_STEALTH);
		}
		if (conditionMask & anim_condition_flags::Swim)
		{
			pushProtoOverride(proto_client::ANIM_COND_SWIM);

			SetRef swim;
			swim.builtin = builtinSwim;
			swim.builtinCount = std::size(builtinSwim);
			swim.samples = builtinSwimSamples;
			swim.sampleCount = std::size(builtinSwimSamples);
			stack.push_back(swim);
		}
		if (conditionMask & anim_condition_flags::Combat2H)
		{
			pushProtoOverride(proto_client::ANIM_COND_COMBAT_2H);
		}
		if (conditionMask & anim_condition_flags::Combat1H)
		{
			pushProtoOverride(proto_client::ANIM_COND_COMBAT_1H);
		}
		if (conditionMask & anim_condition_flags::CombatUnarmed)
		{
			pushProtoOverride(proto_client::ANIM_COND_COMBAT_UNARMED);
		}
		if (conditionMask & anim_condition_flags::Combat)
		{
			pushProtoOverride(proto_client::ANIM_COND_COMBAT);
		}
		if (conditionMask & anim_condition_flags::Walk)
		{
			pushProtoOverride(proto_client::ANIM_COND_WALK);

			SetRef walk;
			walk.builtin = builtinWalk;
			walk.builtinCount = std::size(builtinWalk);
			walk.samples = builtinWalkSamples;
			walk.sampleCount = std::size(builtinWalkSamples);
			stack.push_back(walk);
		}

		if (profile && profile->has_base())
		{
			SetRef base;
			base.proto = &profile->base();
			stack.push_back(base);
		}

		SetRef builtin;
		builtin.builtin = builtinBase;
		builtin.builtinCount = std::size(builtinBase);
		builtin.samples = builtinBaseSamples;
		builtin.sampleCount = std::size(builtinBaseSamples);
		stack.push_back(builtin);

		const auto resolveClip = [&](const String& name) -> AnimationState*
		{
			if (name.empty() || !m_sourceSet->HasAnimationState(name))
			{
				return nullptr;
			}
			return m_sourceSet->GetAnimationState(name);
		};

		// Resolve each slot through the stack. Runtime binds (weapon ready stance, special
		// idle emote) take precedence over everything else.
		for (uint32 slot = 0; slot < SlotCount; ++slot)
		{
			AnimationState* resolved = nullptr;
			bool hasExplicitPlayRate = false;
			float playRate = 1.0f;

			if (slot == proto_client::ANIM_SLOT_COMBAT_IDLE)
			{
				resolved = resolveClip(runtimeBinds.combatReadyClip);
			}
			else if (slot == proto_client::ANIM_SLOT_SPECIAL_IDLE)
			{
				resolved = resolveClip(runtimeBinds.specialIdleClip);
			}

			for (const SetRef& set : stack)
			{
				if (resolved)
				{
					break;
				}

				if (set.proto)
				{
					if (const auto* binding = FindProtoBinding(*set.proto, slot))
					{
						resolved = resolveClip(binding->clip());
						if (resolved && binding->has_play_rate())
						{
							hasExplicitPlayRate = true;
							playRate = binding->play_rate();
						}
					}

					// A set that overrides movement captures the whole movement family: missing
					// directional clips fall back to the set's forward clip so the unit never
					// mixes e.g. stealth-forward with regular strafing.
					if (!resolved && IsMovementSlot(slot) && ProtoSetDefinesMovement(*set.proto))
					{
						if (const auto* forward = FindProtoBinding(*set.proto, proto_client::ANIM_SLOT_MOVE_FORWARD))
						{
							resolved = resolveClip(forward->clip());
						}
					}
				}
				else if (set.builtin)
				{
					for (size_t i = 0; i < set.builtinCount && !resolved; ++i)
					{
						if (static_cast<uint32>(set.builtin[i].slot) != slot)
						{
							continue;
						}

						for (const char* candidate : set.builtin[i].candidates)
						{
							if (!candidate)
							{
								break;
							}

							resolved = resolveClip(candidate);
							if (resolved)
							{
								break;
							}
						}
					}
				}
			}

			m_bindings[slot] = resolved;
			if (resolved)
			{
				resolved->SetLoop(IsLoopingSlot(slot));
				if (hasExplicitPlayRate)
				{
					resolved->SetPlayRate(playRate);
				}
			}
		}

		// Terminal fallback chains, evaluated on the fully resolved table.
		if (!m_bindings[proto_client::ANIM_SLOT_MOVE_FORWARD])
		{
			m_bindings[proto_client::ANIM_SLOT_MOVE_FORWARD] = m_bindings[proto_client::ANIM_SLOT_IDLE];
		}
		for (uint32 slot = proto_client::ANIM_SLOT_MOVE_BACKWARD; slot <= proto_client::ANIM_SLOT_MOVE_BACKWARD_RIGHT; ++slot)
		{
			if (!m_bindings[slot])
			{
				m_bindings[slot] = m_bindings[proto_client::ANIM_SLOT_MOVE_FORWARD];
			}
		}
		if (!m_bindings[proto_client::ANIM_SLOT_COMBAT_IDLE])
		{
			m_bindings[proto_client::ANIM_SLOT_COMBAT_IDLE] = m_bindings[proto_client::ANIM_SLOT_IDLE];
		}
		if (!m_bindings[proto_client::ANIM_SLOT_JUMP_START])
		{
			m_bindings[proto_client::ANIM_SLOT_JUMP_START] = m_bindings[proto_client::ANIM_SLOT_FALL];
		}

		// One-shot clips must start from the beginning when they play next. Skip clips that
		// are currently playing - the table also rebuilds on condition changes (weapon drawn,
		// swim, ...) and must not restart an active death or flinch animation.
		if (AnimationState* death = m_bindings[proto_client::ANIM_SLOT_DEATH]; death && !death->IsEnabled())
		{
			death->SetTimePosition(0.0f);
		}
		if (AnimationState* hit = m_bindings[proto_client::ANIM_SLOT_HIT]; hit && !hit->IsEnabled())
		{
			hit->SetTimePosition(0.0f);
		}

		// Build the directional movement blend space from the highest-priority set that
		// defines movement and has at least a usable forward clip.
		for (const SetRef& set : stack)
		{
			if (set.proto && set.proto->has_movement() && set.proto->movement().clips_size() > 0)
			{
				m_movementBlendSpace.SetMaxBlendAngle(set.proto->movement().max_blend_angle());
				for (const auto& sample : set.proto->movement().clips())
				{
					if (AnimationState* state = resolveClip(sample.clip()))
					{
						state->SetLoop(true);
						m_movementBlendSpace.AddSample(sample.angle(), *state);
					}
				}
			}
			else if (set.proto && ProtoSetDefinesMovement(*set.proto))
			{
				m_movementBlendSpace.SetMaxBlendAngle(100.0f);
				for (const auto& binding : set.proto->bindings())
				{
					if (!IsMovementSlot(binding.slot()))
					{
						continue;
					}

					if (AnimationState* state = resolveClip(binding.clip()))
					{
						state->SetLoop(true);
						m_movementBlendSpace.AddSample(SlotAngle(binding.slot()), *state);
					}
				}
			}
			else if (set.samples)
			{
				m_movementBlendSpace.SetMaxBlendAngle(100.0f);
				for (size_t i = 0; i < set.sampleCount; ++i)
				{
					if (AnimationState* state = resolveClip(set.samples[i].clip))
					{
						state->SetLoop(true);
						m_movementBlendSpace.AddSample(set.samples[i].angle, *state);
					}
				}
			}
			else
			{
				continue;
			}

			if (!m_movementBlendSpace.IsEmpty())
			{
				break;
			}

			m_movementBlendSpace.Clear();
		}
		m_movementBlendSpace.Finalize();

		// Resolve weapon auto attack clips (one-shots picked at random per swing).
		for (const String& name : runtimeBinds.mainHandAttackClips)
		{
			if (AnimationState* state = resolveClip(name))
			{
				state->SetLoop(false);
				m_mainHandAttackStates.push_back(state);
			}
		}
		for (const String& name : runtimeBinds.offHandAttackClips)
		{
			if (AnimationState* state = resolveClip(name))
			{
				state->SetLoop(false);
				m_offHandAttackStates.push_back(state);
			}
		}
	}

	void SlotBindingTable::Invalidate()
	{
		m_bindings.fill(nullptr);
		m_movementBlendSpace.Clear();
		m_mainHandAttackStates.clear();
		m_offHandAttackStates.clear();
		m_sourceSet = nullptr;
	}

	bool SlotBindingTable::IsStale(const Entity* entity) const
	{
		const AnimationStateSet* currentSet = entity ? entity->GetAllAnimationStates() : nullptr;
		return m_sourceSet != currentSet;
	}

	AnimationState* SlotBindingTable::Get(const proto_client::AnimationSlot slot) const
	{
		ASSERT(static_cast<uint32>(slot) < SlotCount);
		return m_bindings[slot];
	}
}
