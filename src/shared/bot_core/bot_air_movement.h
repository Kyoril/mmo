// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/traversal.h"
#include "math/vector3.h"

#include <vector>

namespace mmo
{
	/// Upward speed of a bot jump, in units per second.
	constexpr float BotJumpSpeed = 7.0f;

	/// How often an airborne bot sends a heartbeat, in milliseconds.
	constexpr GameTime BotAirHeartbeatMs = 250;

	/// @brief Air physics of the headless bot: a jump, the fall after it and gliding, until the bot
	///	reaches a known landing height. Gliding uses the shared glide model (StepGlideVelocity) the
	///	server validates glides against; falling uses the server's fall gravity.
	class BotAirMovement final
	{
	public:
		/// Result of one Advance.
		struct Step
		{
			/// Position after the step.
			Vector3 position;

			/// Velocity after the step.
			Vector3 velocity;

			/// Whether the bot reached its landing height in this step.
			bool landed = false;
		};

	public:
		/// @brief Starts a jump: the bot leaves the ground with the given upward speed.
		/// @param position Where the jump starts.
		/// @param jumpSpeed Upward speed at the start.
		/// @param landingHeight Height at which the bot lands while descending.
		void Jump(const Vector3& position, float jumpSpeed, float landingHeight);

		/// @brief Opens the glider (only while airborne).
		void OpenGlider();

		/// @brief Closes the glider; the bot falls on.
		void CloseGlider();

		/// @brief Sets the horizontal direction the glider steers to (zero: no input).
		void SetGlideDirection(const Vector3& horizontalDirection);

		/// @brief Ends any air movement at once (teleport, world change).
		void Reset();

		/// @brief Advances the air movement.
		/// @param seconds Time step.
		/// @param updrafts Updraft volumes of the map.
		/// @param settings Glide settings.
		/// @return The new state; landed is true in the step that reaches the landing height.
		Step Advance(float seconds, const std::vector<UpdraftVolume>& updrafts, const TraversalSettings& settings);

		/// @brief Whether the bot is in the air.
		[[nodiscard]] bool IsAirborne() const { return m_airborne; }

		/// @brief Whether the glider is open.
		[[nodiscard]] bool IsGliding() const { return m_airborne && m_gliding; }

		/// @brief Current velocity.
		[[nodiscard]] const Vector3& GetVelocity() const { return m_velocity; }

		/// @brief Current position.
		[[nodiscard]] const Vector3& GetPosition() const { return m_position; }

	private:
		Vector3 m_position;
		Vector3 m_velocity;
		Vector3 m_glideDirection;
		float m_landingHeight = 0.0f;
		bool m_airborne = false;
		bool m_gliding = false;
	};
}
