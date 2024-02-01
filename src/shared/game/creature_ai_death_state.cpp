
#include "creature_ai_death_state.h"
#include "creature_ai.h"
#include "game_creature_s.h"
#include "log/default_log_levels.h"

namespace mmo
{
	CreatureAIDeathState::CreatureAIDeathState(CreatureAI& ai)
		: CreatureAIState(ai)
	{
	}

	CreatureAIDeathState::~CreatureAIDeathState()
	{
	}

	void CreatureAIDeathState::OnEnter()
	{
		CreatureAIState::OnEnter();

		auto& controlled = GetControlled();

		// TODO: Grant rewards to players

		// Despawn in 30 seconds
		controlled.TriggerDespawnTimer(30000);
	}

	void CreatureAIDeathState::OnLeave()
	{
		CreatureAIState::OnLeave();
	}

}
