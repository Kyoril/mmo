// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "creature_ai_state.h"
#include "base/timer_queue.h"
#include "base/countdown.h"

namespace mmo
{
	/// Handle the preparation state of a creature AI. Creatures enter this state
	/// immediately after they spawned. In this state, creature start casting their
	/// respective spells on themself and they wont aggro nearby enemies if they
	/// come too close.
	class CreatureAIPrepareState : public CreatureAIState
	{
	public:
		/// Initializes a new instance of the CreatureAIIdleState class.
		/// @param ai The ai class instance this state belongs to.
		explicit CreatureAIPrepareState(CreatureAI& ai);
		/// Default destructor.
		virtual ~CreatureAIPrepareState();

	public:
		/// @copydoc CreatureAIState::OnEnter
		virtual void OnEnter() override;

		/// @copydoc CreatureAIState::OnLeave
		virtual void OnLeave() override;

		/// @copydoc CreatureAIState::OnDamage
		virtual void OnDamage(GameUnitS& attacker) override;

	private:

		scoped_connection m_onThreatened;
		Countdown m_preparation;
	};
}
