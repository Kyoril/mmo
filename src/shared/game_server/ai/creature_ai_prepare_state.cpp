// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/ai/creature_ai_prepare_state.h"

#include "game_server/ai/creature_ai.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/world/universe.h"

namespace mmo
{
	CreatureAIPrepareState::CreatureAIPrepareState(CreatureAI& ai)
		: CreatureAIState(ai)
		, m_preparation(ai.GetControlled().GetTimers())
	{
	}

	CreatureAIPrepareState::~CreatureAIPrepareState()
	{
	}

	void CreatureAIPrepareState::OnEnter()
	{
		CreatureAIState::OnEnter();

		// Update home position
		GetAI().SetHome(
			CreatureAI::Home(
			GetAI().GetControlled().GetPosition(),
			GetAI().GetControlled().GetFacing().GetValueRadians()));

		m_preparation.ended.connect([this]()
			{
				// Enter idle state
				GetAI().Idle();
			});

		// Start preparation timer (3 seconds)
		m_preparation.SetEnd(GetAsyncTimeMs() + constants::OneSecond * 6);

		// Watch for threat events to enter combat
		auto& ai = GetAI();
		m_onThreatened = GetControlled().threatened.connect(std::bind(&CreatureAI::OnThreatened, &ai, std::placeholders::_1, std::placeholders::_2));
	}

	void CreatureAIPrepareState::OnLeave()
	{
		CreatureAIState::OnLeave();
	}

	void CreatureAIPrepareState::OnDamage(GameUnitS& attacker)
	{
		CreatureAIState::OnDamage(attacker);
		GetAI().EnterCombat(attacker);
	}
}
