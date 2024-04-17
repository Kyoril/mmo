
#include "creature_ai_combat_state.h"
#include "creature_ai.h"
#include "game_creature_s.h"

namespace mmo
{
	CreatureAICombatState::CreatureAICombatState(CreatureAI& ai, GameUnitS& victim)
		: CreatureAIState(ai)
		, m_combatInitiator(std::static_pointer_cast<GameUnitS>(victim.shared_from_this()))
	{
	}

	CreatureAICombatState::~CreatureAICombatState()
	{
	}

	void CreatureAICombatState::OnEnter()
	{
		CreatureAIState::OnEnter();

		if (const auto initiator = m_combatInitiator.lock())
		{
			GetControlled().AddCombatParticipant(*initiator);
		}
	}

	void CreatureAICombatState::OnLeave()
	{
		CreatureAIState::OnLeave();
	}

	void CreatureAICombatState::OnDamage(GameUnitS& attacker)
	{
		CreatureAIState::OnDamage(attacker);

		GetControlled().AddCombatParticipant(attacker);
	}

	void CreatureAICombatState::OnCombatMovementChanged()
	{
		CreatureAIState::OnCombatMovementChanged();
	}

	void CreatureAICombatState::OnControlledMoved()
	{
		CreatureAIState::OnControlledMoved();
	}
}
