// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <functional>
#include <vector>

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include "animation_binding.h"
#include "animation_context.h"
#include "animation_layers.h"

namespace mmo
{
	class Entity;

	namespace proto_client
	{
		class Project;
	}

	/// @brief Drives all skeletal animation of a game unit through a layered state model:
	///	a locomotion layer (idle / directional movement / swim / jump / death) whose logical
	///	slots are bound to concrete clips by a data-driven animation profile with condition
	///	override sets (walk, swim, stealth, combat by weapon type), an action layer for
	///	one-shot animations (attack swings, emotes, hit flinches), a pose layer for locked
	///	loops (sit/sleep poses, spell channels) and a bone-masked face overlay layer.
	///
	///	GameUnitC feeds a per-frame AnimationContext snapshot and forwards gameplay events
	///	(swings, emotes, pose changes); the controller owns every animation weight.
	class AnimationController final : public NonCopyable
	{
	public:
		/// @brief Creates the controller.
		/// @param entity Reference to the owning unit's entity pointer. The pointer may be
		///	null and may change on display/mesh swaps; the controller re-resolves its clip
		///	bindings automatically when that happens.
		/// @param project The client data project used to look up animation profiles.
		explicit AnimationController(Entity*& entity, const proto_client::Project& project);

		/// @brief Sets the active animation profile (nullptr = built-in default clip names).
		void SetProfile(const proto_client::AnimationProfileEntry* profile);

		/// @brief Looks up and sets the animation profile by id (0 = built-in defaults).
		void SetProfileId(uint32 profileId);

		/// @brief Notifies the controller that the entity's mesh (and with it the whole
		///	animation state set) is about to change or has changed. Drops all clip pointers.
		void NotifyMeshChanged();

		/// @brief Per-frame update: evaluates the layer stack from the given context and
		///	advances all animation weights and time positions. The single entry point.
		void Update(const AnimationContext& ctx);

	public:
		// Action layer (one-shot animations)

		/// @brief Plays a one-shot animation in front of the locomotion layer.
		/// @param state The animation state to play (must not have the loop flag set).
		/// @param suppressIfBusy When true and another one-shot is still in its first half,
		///	the request is silently dropped (off-hand swings). When false, it is queued to
		///	start right after the current one finishes.
		/// @return True if the animation actually started.
		bool PlayAction(AnimationState* state, bool suppressIfBusy = false);

		/// @brief Plays a pose enter/exit transition clip through the action layer. Marked so
		///	movement can fast-forward it without affecting attack or emote one-shots.
		bool PlayPoseTransition(AnimationState* state);

		/// @brief Cancels the current one-shot (and any pending one).
		void CancelAction();

		/// @brief Returns true while a one-shot animation contributes to the pose.
		[[nodiscard]] bool IsActionPlaying() const { return m_action.IsPlaying(); }

		/// @brief Queues a callback for the current one-shot's SwingHit notify (or its end).
		///	Fires immediately when no one-shot is playing.
		void QueueActionHitCallback(std::function<void()> callback);

		/// @brief Fires all queued swing-hit callbacks (called from the SwingHit notify).
		void NotifyActionHit();

		/// @brief Plays an auto-attack swing animation from the equipped weapon's clip list
		///	(one picked at random), falling back to the unarmed attack clip.
		/// @param offhand True for off-hand (dual wield) swings, which use the off-hand list
		///	and are suppressed while the main-hand swing is still fresh.
		/// @return True if a new animation actually started.
		bool PlayAttackSwing(bool offhand);

		/// @brief Plays the damage hit flinch animation.
		void PlayHit();

		/// @brief Plays the landing animation and re-arms the jump start clip.
		void OnLanded();

	public:
		// Pose layer (locked loops)

		/// @brief Sets or clears (nullptr) the looping spell channel animation that replaces
		///	locomotion while active.
		void SetSpellLoopAnimation(AnimationState* state);

		/// @brief Sets the looping pose animation (sit/sleep/kneel) and locks it.
		void SetPoseLoop(AnimationState& state);

		/// @brief Releases the pose loop (stand up). A spell channel lock survives.
		void ClearPoseLoop();

		/// @brief Returns true while the pose loop currently locks the body animation.
		[[nodiscard]] bool IsPoseLockActive() const { return m_pose.IsPoseLockActive(); }

	public:
		// Face overlay layer

		/// @brief Sets (or clears with nullptr) the looping mood clip, masked to face bones.
		void SetMoodClip(AnimationState* state);

	public:
		// Runtime clip binds (weapon item data, special idle emote)

		/// @brief Sets the combat-ready stance clip name of the equipped weapon (empty =
		///	fall back to the profile / built-in ready animation).
		void SetCombatReadyClip(const String& clipName);

		/// @brief Sets the main-hand auto attack clip names of the equipped weapon.
		void SetAttackClips(std::vector<String> clipNames);

		/// @brief Sets the off-hand auto attack clip names of the equipped off-hand weapon.
		void SetOffhandAttackClips(std::vector<String> clipNames);

		/// @brief Sets the special idle clip name from the unit's IdlePoseEmote selection.
		void SetSpecialIdleClip(const String& clipName);

		/// @brief Restarts the stationary-idle timer that eases into the special idle pose.
		void ResetIdleTimer() { m_idleSeconds = 0.0f; }

	private:
		/// @brief Rebuilds the slot binding table when the mesh, profile, condition set or
		///	runtime binds changed since the last build.
		void EnsureBindings();

		/// @brief Drops stale clip pointers from all layers after a mesh swap.
		void ValidateLayers();

		/// @brief Computes the active condition bit mask from the context.
		[[nodiscard]] static uint32 ComputeConditionMask(const AnimationContext& ctx);

		/// @brief Derives the movement direction angle (degrees, 0 = forward, 90 = right)
		///	from the movement flags.
		[[nodiscard]] static float MovementAngleFromFlags(uint32 movementFlags);

		/// @brief Evaluates the locomotion state machine into a desired weighted clip set.
		void EvaluateLocomotion(const AnimationContext& ctx);

		/// @brief Shared one-shot start path (eviction rules, busy handling).
		bool PlayActionInternal(AnimationState* state, bool suppressIfBusy, bool isPoseTransition);

	private:
		Entity*& m_entity;
		const proto_client::Project& m_project;
		const proto_client::AnimationProfileEntry* m_profile{nullptr};

		SlotBindingTable m_bindings;
		AnimationRuntimeBinds m_runtimeBinds;
		uint32 m_conditionMask{0};
		bool m_bindingsDirty{true};

		LocomotionLayer m_locomotion;
		ActionLayer m_action;
		PoseLayer m_pose;
		FaceOverlayLayer m_face;

		/// Seconds the unit has been standing still; drives the special idle transition.
		float m_idleSeconds{0.0f};
	};
}
