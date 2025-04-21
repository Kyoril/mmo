// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "creature_ai_state.h"
#include "base/signal.h"

namespace mmo
{
	/// Handle the idle state of a creature AI. In this state, most units
	/// watch for hostile units which come close enough, and start attacking these
	/// units.
	class CreatureAIDeathState : public CreatureAIState
	{
	public:
		/// Initializes a new instance of the CreatureAIIdleState class.
		/// @param ai The ai class instance this state belongs to.
		explicit CreatureAIDeathState(CreatureAI& ai);
		/// Default destructor.
		virtual ~CreatureAIDeathState();

	public:
		/// @copydoc CreatureAIState::OnEnter
		virtual void OnEnter() override;

		/// @copydoc CreatureAIState::OnLeave
		virtual void OnLeave() override;

	private:
		scoped_connection m_onLootCleared;
	};
}
