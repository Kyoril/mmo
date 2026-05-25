// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/countdown.h"
#include "base/signal.h"
#include "math/vector3.h"

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
		// Wander radius for disoriented units
		static constexpr float kDisorientRadius = 4.0f;
		// Milliseconds between wander ticks
		static constexpr int kWanderTickMs = 1500;

		// Fear flee parameters:
		/// Distance ahead of the unit (in the flee direction) where the navmesh
		/// sample circle is centred. Units will flee roughly this far per tick.
		static constexpr float kFearProjectionDist = 12.0f;
		/// Radius of the navmesh sample circle around the projected point.
		/// Larger = more variation in path; smaller = straighter flee.
		static constexpr float kFearSampleRadius = 4.0f;
		/// Fallback radius used when the directional sample fails (wall/cliff
		/// ahead in the flee direction).
		static constexpr float kFearFallbackRadius = 8.0f;

	public:
		explicit CCMovementController(GameUnitS& unit);
		~CCMovementController() = default;

		// Non-copyable / non-movable — holds references and signal connections
		CCMovementController(const CCMovementController&) = delete;
		CCMovementController& operator=(const CCMovementController&) = delete;

	public:
		/// Activate forced wander. Captures the flee-from position (attacker /
		/// victim) and the unit's current position so fear movement can be
		/// directionally biased away from the caster.
		/// If the unit is currently suppressed (stunned / sleeping / rooted) the
		/// controller marks itself active but does NOT schedule movement —
		/// resumption happens via OnWanderTick().
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

		/// Position of the unit when fear started (the origin we flee from).
		Vector3 m_fearOrigin;
		/// World position of the fear source (caster) when fear was applied.
		/// Zero-vector when unknown (no attacker could be found).
		Vector3 m_fearSourcePos;
		/// True when m_fearSourcePos was successfully captured from a live attacker.
		bool m_hasFearSource = false;
	};
}
