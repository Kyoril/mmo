// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "creature_ai_state.h"
#include "game_server/world/unit_watcher.h"
#include "base/countdown.h"

namespace mmo
{
	class GameObjectS;

	/// Handle the idle state of a creature AI. In this state, most units
	/// watch for hostile units which come close enough, and start attacking these
	/// units.
	class CreatureAIIdleState : public CreatureAIState
	{
	public:
		/// Initializes a new instance of the CreatureAIIdleState class.
		/// @param ai The ai class instance this state belongs to.
		explicit CreatureAIIdleState(CreatureAI& ai);
		/// Default destructor.
		virtual ~CreatureAIIdleState();

	public:
		/// @copydoc CreatureAIState::OnEnter
		virtual void OnEnter() override;

		/// @copydoc CreatureAIState::OnLeave
		virtual void OnLeave() override;

		/// @copydoc CreatureAIState::OnCreatureMovementChanged
		virtual void OnCreatureMovementChanged() override;

		/// @copydoc CreatureAIState::OnControlledMoved
		virtual void OnControlledMoved() override;

		virtual void OnDamage(GameUnitS& attacker) override;

	protected:
		void OnWaitCountdownExpired();

		void OnTargetReached();

		void MoveToRandomPointInRange();

	private:

		Countdown m_waitCountdown;

		scoped_connection_container m_connections;

		std::unique_ptr<UnitWatcher> m_unitWatcher;
	};
}
