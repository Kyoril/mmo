// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <functional>
#include <vector>

#include "base/typedefs.h"

#include "animation_blend_space.h"

namespace mmo
{
	class AnimationState;
	class AnimationStateSet;
	class Entity;

	/// @brief Locomotion layer: drives the weighted set of movement/idle clips with a
	///	continuous per-clip weight solver. The desired output of the controller's state
	///	evaluation (one clip, or two blend-space clips) is approached at crossfade speed;
	///	clips no longer desired fade out and are disabled at weight zero.
	class LocomotionLayer final
	{
	public:
		/// @brief Sets the desired weighted clip set. Passing zero clips keeps the previous
		///	desired set (mirrors the legacy behavior of ignoring null target states).
		void SetDesired(const WeightedAnimClip* clips, uint32 count);

		/// @brief Ramps the actual clip weights toward the desired set.
		/// @param deltaTime Frame time in seconds.
		/// @param visibilityCap Upper bound for all locomotion weights; the action layer
		///	pushes this to 0 while a one-shot animation plays.
		/// @param fadeDuration Crossfade duration in seconds.
		void ApplyWeights(float deltaTime, float visibilityCap, float fadeDuration);

		/// @brief Advances the time position of all active clips.
		void AdvanceTime(float deltaTime) const;

		/// @brief Drops all clip pointers without touching the states (mesh about to change).
		void Reset();

		/// @brief Drops stale clip pointers that no longer belong to the given state set.
		void ValidateAgainst(const AnimationStateSet* set);

	private:
		struct ActiveClip
		{
			AnimationState* state{nullptr};
			float weight{0.0f};
		};

		[[nodiscard]] float DesiredWeightOf(const AnimationState& state) const;

		std::vector<ActiveClip> m_active;
		WeightedAnimClip m_desired[2];
		uint32 m_desiredCount{0};
	};

	/// @brief Action layer: plays one-shot animations (attack swings, emotes, hit flinches,
	///	pose transitions) in front of the locomotion layer. Ports the legacy one-shot
	///	machinery: a one-deep pending queue, the suppress-if-busy rule for off-hand swings,
	///	fade-out after the clip ends, and deferred swing-hit callbacks.
	class ActionLayer final
	{
	public:
		/// @brief Returns true while a one-shot is playing and still in its first half.
		[[nodiscard]] bool IsBusyFirstHalf() const;

		/// @brief Queues a one-shot to start when the current one finishes (replaces any
		///	previously pending entry).
		void SetPending(AnimationState& state);

		/// @brief Starts a one-shot immediately, evicting the current one (flushing its
		///	pending hit callbacks first).
		void Start(AnimationState& state, bool isPoseTransition);

		/// @brief Stops the current one-shot and clears any pending entry and hit callbacks.
		void Cancel();

		/// @brief Advances the end-of-clip fade-out.
		/// @return A pending one-shot that should be started now that the slot is free, or nullptr.
		AnimationState* Update(float deltaTime, float fadeDuration);

		/// @brief Returns true while a one-shot contributes to the pose (playing or fading out).
		[[nodiscard]] bool IsPlaying() const;

		/// @brief Returns true while a one-shot occupies the slot (even when faded out).
		[[nodiscard]] bool IsActive() const { return m_current != nullptr; }

		/// @brief Returns the current one-shot state (nullptr when idle).
		[[nodiscard]] AnimationState* GetCurrent() const { return m_current; }

		/// @brief Returns true when the current one-shot is a pose enter/exit transition clip.
		[[nodiscard]] bool CurrentIsPoseTransition() const { return m_current != nullptr && m_currentIsPoseTransition; }

		/// @brief Clears the pose-transition marker (e.g. on death).
		void ClearPoseTransitionFlag() { m_currentIsPoseTransition = false; }

		/// @brief Fast-forwards the current one-shot to its end (movement caught a pose
		///	transition mid-play, or the unit died).
		void FastForwardCurrent() const;

		/// @brief Upper bound for locomotion weights: 0 while a one-shot plays, ramping back
		///	to 1 while it fades out after ending.
		[[nodiscard]] float LocomotionVisibility() const;

		/// @brief Queues a callback for the current one-shot's SwingHit notify (or its end).
		///	Fires immediately when no one-shot is playing.
		void QueueHitCallback(std::function<void()> callback);

		/// @brief Fires and clears all queued hit callbacks.
		void FlushHitCallbacks();

		/// @brief Drops all clip pointers and queued callbacks (mesh about to change).
		void Reset();

		/// @brief Drops stale clip pointers that no longer belong to the given state set.
		void ValidateAgainst(const AnimationStateSet* set);

	private:
		AnimationState* m_current{nullptr};
		AnimationState* m_pending{nullptr};
		bool m_currentIsPoseTransition{false};
		std::vector<std::function<void()>> m_hitCallbacks;
	};

	/// @brief Pose layer: owns the locked loop animation that replaces locomotion entirely -
	///	either a pose loop (sit/sleep stand states) or a looping spell channel animation.
	///	The pose lock survives one-shots (flinches play in front of the pose and blend back);
	///	a spell lock is evicted when a one-shot starts, mirroring the legacy behavior.
	class PoseLayer final
	{
	public:
		/// @brief Sets or clears the looping spell channel animation. Clearing also releases
		///	a pose lock held through the same slot (legacy behavior of SetLockedLoopAnimation).
		void SetSpellLoop(AnimationState* state);

		/// @brief Sets the pose loop (sit/sleep/kneel) and locks it.
		void SetPoseLoop(AnimationState& state);

		/// @brief Releases the pose loop. A spell lock held through the same slot survives.
		void ClearPoseLoop();

		/// @brief Evicts a locked spell loop when a one-shot starts; the pose lock survives.
		void EvictSpellLoop();

		/// @brief Returns the animation that currently locks the body, or nullptr.
		[[nodiscard]] AnimationState* GetLock() const { return m_lockedLoop; }

		/// @brief Returns true while the active lock is the pose loop.
		[[nodiscard]] bool IsPoseLockActive() const { return m_lockedLoop != nullptr && m_lockedLoop == m_poseLoop; }

		/// @brief Drops all clip pointers (mesh about to change or death).
		void Reset();

		/// @brief Drops stale clip pointers that no longer belong to the given state set.
		void ValidateAgainst(const AnimationStateSet* set);

	private:
		AnimationState* m_lockedLoop{nullptr};
		AnimationState* m_poseLoop{nullptr};
	};

	/// @brief Face overlay layer: plays the looping mood clip restricted to bones whose
	///	names start with "face_" (case-insensitive), layered over whatever the body does.
	class FaceOverlayLayer final
	{
	public:
		/// @brief Sets (or clears with nullptr) the mood clip. Builds the face bone blend
		///	mask; silently no-ops when the mesh has no face bones.
		void SetMoodClip(AnimationState* state, const Entity* entity);

		/// @brief Advances the mood clip time.
		void AdvanceTime(float deltaTime) const;

		/// @brief Drops the clip pointer (mesh about to change).
		void Reset();

		/// @brief Drops the clip pointer when it no longer belongs to the given state set.
		void ValidateAgainst(const AnimationStateSet* set);

	private:
		AnimationState* m_state{nullptr};
	};
}
