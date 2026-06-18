// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "training_dummy_combat_script.h"
#include "game_server/ai/creature_ai_combat_state.h"
#include "game_server/ai/creature_combat_script_registry.h"
#include "objects/game_creature_s.h"

namespace mmo
{
	TrainingDummyCombatScript::TrainingDummyCombatScript(CreatureAICombatState& combatState)
		: CreatureCombatScript(combatState)
		, m_lastDamageTime(0)
	{
	}

	TrainingDummyCombatScript::~TrainingDummyCombatScript() = default;

	void TrainingDummyCombatScript::OnCombatStart()
	{
		m_lastDamageTime = GetAsyncTimeMs();

		// Stop auto attack - training dummies don't fight back
		StopAutoAttack();
		StopMovement();

		// Schedule periodic reset checks
		ScheduleTimer(TIMER_RESET_CHECK, RESET_TIMEOUT_MS);
	}

	bool TrainingDummyCombatScript::OnChooseAction()
	{
		// Training dummies do nothing - just stand there.
		// Return true to prevent the default AI from chasing or attacking.
		return true;
	}

	void TrainingDummyCombatScript::OnDamageTaken(GameUnitS& attacker)
	{
		m_lastDamageTime = GetAsyncTimeMs();

		// Reset the idle timer since we just took damage
		ScheduleTimer(TIMER_RESET_CHECK, RESET_TIMEOUT_MS);
	}

	bool TrainingDummyCombatScript::CanDie() const
	{
		return false;
	}

	bool TrainingDummyCombatScript::CanMove() const
	{
		return false;
	}

	bool TrainingDummyCombatScript::ShouldAutoAttack() const
	{
		return false;
	}

	bool TrainingDummyCombatScript::ShouldResetCombat() const
	{
		// Reset if we haven't taken damage for RESET_TIMEOUT_MS
		const GameTime now = GetAsyncTimeMs();
		if (m_lastDamageTime > 0 && (now - m_lastDamageTime) >= RESET_TIMEOUT_MS)
		{
			return true;
		}

		return false;
	}
}
