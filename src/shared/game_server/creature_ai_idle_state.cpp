
#include "creature_ai_idle_state.h"
#include "creature_ai.h"
#include "game_creature_s.h"
#include "world_instance.h"

namespace mmo
{
	CreatureAIIdleState::CreatureAIIdleState(CreatureAI& ai)
		: CreatureAIState(ai)
		, m_waitCountdown(ai.GetControlled().GetTimers())
	{
	}

	CreatureAIIdleState::~CreatureAIIdleState()
	{
	}

	void CreatureAIIdleState::OnEnter()
	{
		CreatureAIState::OnEnter();

		m_connections += m_waitCountdown.ended.connect(*this, &CreatureAIIdleState::OnWaitCountdownExpired);
		m_connections += GetAI().GetControlled().GetMover().targetReached.connect(*this, &CreatureAIIdleState::OnTargetReached);

		MoveToRandomPointInRange();
	}

	void CreatureAIIdleState::OnLeave()
	{
		m_connections.disconnect();

		CreatureAIState::OnLeave();
	}

	void CreatureAIIdleState::OnCreatureMovementChanged()
	{
	}

	void CreatureAIIdleState::OnControlledMoved()
	{
	}

	void CreatureAIIdleState::OnDamage(GameUnitS& attacker)
	{
		CreatureAIState::OnDamage(attacker);

		GetAI().EnterCombat(attacker);
	}

	void CreatureAIIdleState::OnWaitCountdownExpired()
	{
		MoveToRandomPointInRange();
	}

	void CreatureAIIdleState::OnTargetReached()
	{
		m_waitCountdown.SetEnd(GetAsyncTimeMs() + 2000);
	}

	void CreatureAIIdleState::MoveToRandomPointInRange()
	{
		// Make random movement
		UnitMover& mover = GetAI().GetControlled().GetMover();

		// Generate a random position around 0,0,0 in a x and z radius of 10
		const float x = (rand() % 20) - 10;
		const float z = (rand() % 20) - 10;

		const Vector3 position = GetAI().GetHome().position + Vector3(x, 0.0f, z);
		mover.MoveTo(position);
	}
}
