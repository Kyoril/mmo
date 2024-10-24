
#include "creature_ai_death_state.h"
#include "creature_ai.h"
#include "game_creature_s.h"
#include "loot_instance.h"
#include "log/default_log_levels.h"
#include "proto_data/project.h"

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

		const proto::UnitEntry& entry = controlled.GetEntry();

		// TODO: Calculate correct amount of XP to award to the participant
		const int32 xp = 55;

		controlled.ForEachCombatParticipant([xp](GamePlayerS& player)
			{
				player.RewardExperience(xp);
			});

		std::vector<std::weak_ptr<GamePlayerS>> lootRecipients;
		controlled.ForEachLootRecipient([&controlled, &lootRecipients](std::shared_ptr<GamePlayerS>& character)
			{
				lootRecipients.push_back(character);
			});

		const auto* lootEntry = controlled.GetProject().unitLoot.getById(entry.unitlootentry());

		GameTime despawnDelay = constants::OneSecond * 30;

		// Generate loot
		auto loot = std::make_unique<LootInstance>(controlled.GetProject().items, controlled.GetGuid(), lootEntry, entry.minlootgold(), entry.maxlootgold(), lootRecipients);
		if (!loot->IsEmpty())
		{
			// 3 Minutes of despawn delay if creature still has loot
			despawnDelay = constants::OneMinute * 3;

			// As soon as the loot window is cleared, toggle the flag
			m_onLootCleared = loot->cleared.connect([&controlled]()
				{
					controlled.RemoveFlag<uint32>(object_fields::Flags, unit_flags::Lootable);
				});

			controlled.SetUnitLoot(std::move(loot));
			controlled.AddFlag<uint32>(object_fields::Flags, unit_flags::Lootable);
		}

		// Activate despawn timer
		controlled.TriggerDespawnTimer(despawnDelay);
	}

	void CreatureAIDeathState::OnLeave()
	{
		CreatureAIState::OnLeave();
	}
}
