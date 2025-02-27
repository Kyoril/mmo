
#include "creature_ai_reset_state.h"

#include "game_creature_s.h"
#include "universe.h"

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

		controlled.RemoveLootRecipients();

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

		controlled.GetMover().MoveTo(GetAI().GetHome().position);
	}

	void CreatureAIResetState::OnLeave()
	{
		auto& controlled = GetControlled();

		m_onHomeReached.disconnect();

		// Fully heal unit
		if (controlled.IsAlive())
		{
			controlled.Set<uint32>(object_fields::Health, controlled.GetMaxHealth());
		}

		CreatureAIState::OnLeave();
	}
}
