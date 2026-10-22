// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

#include <vector>

namespace mmo
{
	/// A plaza character hit by the cursor ray.
	struct PlazaPickHit
	{
		/// Order index of the character.
		int32 orderIndex = -1;

		/// Distance along the ray.
		float distance = 0.0f;

		/// False for characters that cannot be picked right now (hidden, walking, leaving).
		bool pickable = true;
	};

	/// Picks the nearest pickable hit.
	/// @return The order index of the nearest pickable hit, or -1 if there is none.
	[[nodiscard]] int32 PickNearestPlazaHit(const std::vector<PlazaPickHit>& hits);

	/// Yaw that turns a character partly towards the camera: the shortest turn from its current yaw
	/// towards the camera, limited to maxTurnDegrees either way.
	/// @param yawDegrees The character's current yaw (facing, including any rotation), in degrees.
	/// @param position The character's position.
	/// @param cameraPosition The camera's position.
	/// @param maxTurnDegrees The largest turn, in degrees (positive).
	/// @return The turn to add to the yaw, in degrees, within [-maxTurnDegrees, maxTurnDegrees].
	[[nodiscard]] float PlazaTurnTowardsDegrees(float yawDegrees, const Vector3& position, const Vector3& cameraPosition, float maxTurnDegrees);

	/// Weight of a one-shot greeting clip over the standing pose: fades in at the start and out
	/// at the end, so the character blends from its pose into the greeting and back.
	/// @param elapsedSeconds Time since the greeting started.
	/// @param lengthSeconds Length of the greeting clip.
	/// @param fadeInSeconds Duration of the fade in.
	/// @param fadeOutSeconds Duration of the fade out.
	/// @return The weight in [0, 1]; 0 outside the clip.
	[[nodiscard]] float PlazaGreetingWeight(float elapsedSeconds, float lengthSeconds, float fadeInSeconds, float fadeOutSeconds);

	/// Whether a pose has the character seated, lying or kneeling, so it neither greets nor turns
	/// (a standing clip over it would pull the character up).
	/// @param pose The pose's clip name.
	[[nodiscard]] bool IsPlazaSeatedPose(const String& pose);

	/// The eased hover reaction of one plaza character: the glow and the turn towards the camera
	/// fade in while the reaction is active and back out when it ends.
	class PlazaHoverReaction final
	{
	public:
		/// Seconds the glow takes to fade fully in or out.
		static constexpr float GlowFadeSeconds = 0.15f;

		/// Seconds the turn towards the camera takes, either way.
		static constexpr float TurnSeconds = 0.35f;

	public:
		/// Advances the reaction.
		/// @param active Whether the character is hovered and may react (standing, visible).
		/// @param deltaSeconds Time since the last update.
		/// @return True when the reaction became active with this update (a hover enter).
		bool Update(bool active, float deltaSeconds);

		/// Drops the reaction at once (the character started walking or was hidden).
		void Reset();

		/// Whether the reaction was active at the last update.
		[[nodiscard]] bool IsActive() const { return m_active; }

		/// Glow strength in [0, 1], eased.
		[[nodiscard]] float GetGlow() const;

		/// Share of the turn towards the camera in [0, 1], eased.
		[[nodiscard]] float GetTurn() const;

		/// Whether nothing of the reaction is left to show.
		[[nodiscard]] bool IsIdle() const { return !m_active && m_glow <= 0.0f && m_turn <= 0.0f; }

	private:
		bool m_active = false;
		float m_glow = 0.0f;
		float m_turn = 0.0f;
	};
}
