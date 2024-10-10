
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

		// Stop movement immediately
		auto& controlled = GetControlled();
		controlled.GetMover().StopMovement();
		controlled.StopAttack();
		controlled.SetTarget(0);

		// TODO: Calculate correct amount of XP to award to the participant
		const int32 xp = 55;

		controlled.ForEachCombatParticipant([xp](GamePlayerS& player)
			{
				player.RewardExperience(xp);
			});

		// Despawn in 30 seconds
		controlled.TriggerDespawnTimer(30000);
	}

	void CreatureAIDeathState::OnLeave()
	{
		CreatureAIState::OnLeave();
	}

}
