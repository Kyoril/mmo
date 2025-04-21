// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/ai/creature_ai_state.h"
#include "game_server/ai/creature_ai.h"
#include "game_server/objects/game_creature_s.h"
#include "base/macros.h"

namespace mmo
{
	CreatureAIState::CreatureAIState(CreatureAI& ai)
		: m_ai(ai)
		, m_isActive(false)
	{
	}

	CreatureAIState::~CreatureAIState()
	{
	}

	CreatureAI& CreatureAIState::GetAI() const
	{
		return m_ai;
	}

	GameCreatureS& CreatureAIState::GetControlled() const
	{
		return m_ai.GetControlled();
	}

	void CreatureAIState::OnEnter()
	{
		ASSERT(!m_isActive);
		m_isActive = true;
	}

	void CreatureAIState::OnLeave()
	{
		ASSERT(m_isActive);
		m_isActive = false;
	}

	void CreatureAIState::OnDamage(GameUnitS& attacker)
	{
	}

	void CreatureAIState::OnHeal(GameUnitS& healer)
	{
	}

	void CreatureAIState::OnControlledDeath()
	{
	}

	void CreatureAIState::OnCombatMovementChanged()
	{
		// Does nothing
	}

	void CreatureAIState::OnCreatureMovementChanged()
	{
		// Does nothing
	}
	void CreatureAIState::OnControlledMoved()
	{
	}
}
