// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "creature_ai_state.h"
#include "base/countdown.h"
#include "base/signal.h"

#include <memory>

namespace mmo
{
	/// Handle the alert state of a creature AI. This state is entered when a creature
	/// detects a stealthed hostile unit: the creature stops moving and turns toward the
	/// unit. If the unit is still visible when the alert time runs out, or comes closer
	/// while being watched, or drops stealth, combat is engaged. Otherwise the creature
	/// returns to its idle state and resumes movement.
	class CreatureAIAlertState final : public CreatureAIState
	{
	public:
		/// Total time the creature stays alerted before deciding (milliseconds).
		static constexpr GameTime AlertDuration = 3000;

		/// Interval between visibility re-checks while alerted (milliseconds).
		static constexpr GameTime AlertCheckInterval = 400;

		/// How much closer (in meters) the target has to come compared to the spot
		/// distance to trigger combat before the alert time runs out.
		static constexpr float EngageDistanceDelta = 1.0f;

	public:
		/// Initializes a new instance of the CreatureAIAlertState class.
		/// @param ai The ai class instance this state belongs to.
		/// @param target The stealthed unit that has been detected.
		explicit CreatureAIAlertState(CreatureAI& ai, GameUnitS& target);

		/// Default destructor.
		~CreatureAIAlertState() override;

	public:
		/// @copydoc CreatureAIState::OnEnter
		void OnEnter() override;

		/// @copydoc CreatureAIState::OnLeave
		void OnLeave() override;

		/// @copydoc CreatureAIState::OnDamage
		void OnDamage(GameUnitS& attacker) override;

	private:
		/// Runs one visibility re-check: engages combat, keeps waiting or gives up.
		void OnCheckTimer();

		/// Posts a combat state transition against the given unit to the universe.
		void PostEnterCombat(const std::shared_ptr<GameUnitS>& target);

		/// Posts an idle state transition to the universe.
		void PostIdle();

	private:
		std::weak_ptr<GameUnitS> m_target;

		Countdown m_checkCountdown;

		scoped_connection_container m_connections;

		GameTime m_alertEnd = 0;

		float m_spotDistanceSq = 0.0f;
	};
}
