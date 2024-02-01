
#include "creature_ai_idle_state.h"
#include "creature_ai.h"
#include "game_creature_s.h"
#include "world_instance.h"

namespace mmo
{
	CreatureAIIdleState::CreatureAIIdleState(CreatureAI& ai)
		: CreatureAIState(ai)
	{
	}

	CreatureAIIdleState::~CreatureAIIdleState()
	{
	}

	void CreatureAIIdleState::OnEnter()
	{
		CreatureAIState::OnEnter();

	}

	void CreatureAIIdleState::OnLeave()
	{
		CreatureAIState::OnLeave();
	}

	void CreatureAIIdleState::OnCreatureMovementChanged()
	{
	}

	void CreatureAIIdleState::OnControlledMoved()
	{
	}
}
