// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/ai/creature_ai_reset_state.h"

#include "game_server/objects/game_creature_s.h"
#include "game_server/world/universe.h"

namespace mmo
{
	CreatureAIResetState::CreatureAIResetState(CreatureAI& ai)
		: CreatureAIState(ai)
	{
	}

	CreatureAIResetState::~CreatureAIResetState()
	= default;

	void CreatureAIResetState::OnEnter()
	{
		CreatureAIState::OnEnter();

		auto& controlled = GetControlled();

		controlled.RaiseTrigger(trigger_event::OnReset);
		controlled.RemoveLootRecipients();
		controlled.SetTarget(0);

		// Enter idle mode when home point was reached
		m_onHomeReached = controlled.GetMover().targetReached.connect([this]() {
			auto& ai = GetAI();
			auto* world = ai.GetControlled().GetWorldInstance();
			if (world)
			{
				auto& universe = world->GetUniverse();
				universe.Post([&ai]() {
					ai.Idle();
					});
			}
			});

		const auto facing = Radian(GetAI().GetHome().orientation);
		controlled.GetMover().MoveTo(GetAI().GetHome().position, 0.0f, &facing);
	}

	void CreatureAIResetState::OnLeave()
	{
		auto& controlled = GetControlled();

		m_onHomeReached.disconnect();
		controlled.RaiseTrigger(trigger_event::OnReachedHome);

		// Fully heal unit
		if (controlled.IsAlive())
		{
			controlled.Set<uint32>(object_fields::Health, controlled.GetMaxHealth());

			// Also restore full mana / energy if applicable
			if (controlled.GetPowerType() == power_type::Mana)
			{
				controlled.Set<uint32>(object_fields::Mana, controlled.GetMaxPower());
			}
			else if (controlled.GetPowerType() == power_type::Energy)
			{
				controlled.Set<uint32>(object_fields::Energy, controlled.GetMaxPower());
			}
			else if (controlled.GetPowerType() == power_type::Rage)
			{
				// Rage is reset to 0 on reset
				controlled.Set<uint32>(object_fields::Rage, 0);
			}
		}

		CreatureAIState::OnLeave();
	}
}
