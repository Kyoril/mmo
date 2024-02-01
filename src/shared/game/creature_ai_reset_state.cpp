
#include "creature_ai_reset_state.h"

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

	}

	void CreatureAIResetState::OnLeave()
	{
		CreatureAIState::OnLeave();
	}

}
