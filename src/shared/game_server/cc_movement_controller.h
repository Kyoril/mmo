// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/countdown.h"
#include "base/signal.h"

namespace mmo
{
	class GameUnitS;

	/// Drives server-controlled forced wander movement for feared / disoriented
	/// non-player units. Fear takes priority over Disorient. Stun / Sleep / Root
	/// suppress movement while the controller remains "active" so that the wander
	/// resumes automatically when the suppression ends.
	class CCMovementController
	{
	public:
		// Wander radius for feared units (yards)
		static constexpr float kFearRadius = 7.0f;
		// Wander radius for disoriented units (yards)
		static constexpr float kDisorientRadius = 4.0f;
		// Milliseconds between wander ticks
		static constexpr int kWanderTickMs = 1500;

	public:
		explicit CCMovementController(GameUnitS& unit);
		~CCMovementController() = default;

		// Non-copyable / non-movable — holds references and signal connections
		CCMovementController(const CCMovementController&) = delete;
		CCMovementController& operator=(const CCMovementController&) = delete;

	public:
		/// Activate forced wander. If the unit is currently suppressed
		/// (stunned / sleeping / rooted) the controller marks itself active but
		/// does NOT schedule movement — resumption happens via OnWanderTick().
		void Start();

		/// Deactivate forced wander and stop any in-flight server movement.
		void Stop();

		/// Returns true while the controller is driving movement (including while
		/// suppression is temporarily pausing actual locomotion).
		bool IsActive() const { return m_active; }

		/// Called by GameUnitS when suppression state changes so the controller
		/// can pause or resume ticking without requiring a full Stop/Start cycle.
		void OnWanderTick();

	private:
		void PickNextWanderPoint();

	private:
		GameUnitS& m_unit;
		Countdown m_wanderCountdown;
		scoped_connection m_targetReachedConn;
		bool m_active = false;
	};
}
