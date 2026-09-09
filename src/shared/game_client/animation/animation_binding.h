// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <array>
#include <vector>

#include "base/typedefs.h"

#include "animation_blend_space.h"
#include "shared/client_data/proto_client/animation_profiles.pb.h"

namespace mmo
{
	class Entity;
	class AnimationState;
	class AnimationStateSet;

	/// @brief Condition bit flags matching proto_client::AnimationCondition values. The set of
	///	active bits selects which profile override sets participate in slot resolution.
	namespace anim_condition_flags
	{
		enum Type : uint32
		{
			None = 0,
			Walk = 1 << proto_client::ANIM_COND_WALK,
			Swim = 1 << proto_client::ANIM_COND_SWIM,
			Stealth = 1 << proto_client::ANIM_COND_STEALTH,
			Combat = 1 << proto_client::ANIM_COND_COMBAT,
			CombatUnarmed = 1 << proto_client::ANIM_COND_COMBAT_UNARMED,
			Combat1H = 1 << proto_client::ANIM_COND_COMBAT_1H,
			Combat2H = 1 << proto_client::ANIM_COND_COMBAT_2H,
		};
	}

	/// @brief Clip names injected at runtime from gameplay data rather than the animation
	///	profile: the equipped weapon's ready/attack animations (item subclass data) and the
	///	unit's selected special idle emote. Names are resolved against the mesh on every
	///	binding table rebuild, so they survive mesh swaps.
	struct AnimationRuntimeBinds
	{
		/// Combat-ready stance clip of the equipped main-hand weapon (empty = profile/default).
		String combatReadyClip;

		/// Special idle clip from the unit's IdlePoseEmote selection (empty = none).
		String specialIdleClip;

		/// Main-hand auto attack swing clips (one picked at random per swing).
		std::vector<String> mainHandAttackClips;

		/// Off-hand auto attack swing clips (dual wield).
		std::vector<String> offHandAttackClips;
	};

	/// @brief Resolves logical animation slots to concrete AnimationState pointers for the
	///	current mesh, profile, condition set and runtime binds. Per slot, the first candidate
	///	whose clip exists on the mesh wins, searched in priority order: runtime binds, active
	///	profile override sets (stealth > swim > combat-by-weapon > combat > walk), the profile
	///	base set, and finally the built-in default clip names that reproduce the legacy
	///	hardcoded behavior. Movement slots additionally resolve into a directional blend space.
	class SlotBindingTable final
	{
	public:
		static constexpr uint32 SlotCount = proto_client::ANIM_SLOT_LOOT + 1;

	public:
		/// @brief Re-resolves all slots against the given entity's animation state set.
		void Rebuild(Entity* entity, const proto_client::AnimationProfileEntry* profile,
			uint32 conditionMask, const AnimationRuntimeBinds& runtimeBinds);

		/// @brief Drops all resolved pointers (mesh about to change).
		void Invalidate();

		/// @brief Returns true when the table was built against a different animation state set
		///	than the entity currently owns (mesh swap) and must be rebuilt before use.
		[[nodiscard]] bool IsStale(const Entity* entity) const;

		/// @brief Returns the resolved animation state for a slot, or nullptr when nothing resolved.
		[[nodiscard]] AnimationState* Get(proto_client::AnimationSlot slot) const;

		/// @brief Returns the directional movement blend space for the active condition set.
		[[nodiscard]] const AnimationBlendSpace& GetMovementBlendSpace() const { return m_movementBlendSpace; }

		/// @brief Returns the condition mask the table was last built with.
		[[nodiscard]] uint32 GetConditionMask() const { return m_conditionMask; }

		/// @brief Returns the crossfade duration in seconds for locomotion transitions.
		[[nodiscard]] float GetFadeDuration() const { return m_fadeDuration; }

		/// @brief Returns the resolved main-hand auto attack states.
		[[nodiscard]] const std::vector<AnimationState*>& GetMainHandAttackStates() const { return m_mainHandAttackStates; }

		/// @brief Returns the resolved off-hand auto attack states.
		[[nodiscard]] const std::vector<AnimationState*>& GetOffHandAttackStates() const { return m_offHandAttackStates; }

	private:
		std::array<AnimationState*, SlotCount> m_bindings{};
		AnimationBlendSpace m_movementBlendSpace;
		std::vector<AnimationState*> m_mainHandAttackStates;
		std::vector<AnimationState*> m_offHandAttackStates;
		const AnimationStateSet* m_sourceSet{nullptr};
		uint32 m_conditionMask{0};
		float m_fadeDuration{0.25f};
	};
}
