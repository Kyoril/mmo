
#include "creature_ai_death_state.h"
#include "creature_ai.h"
#include "game_creature_s.h"
#include "loot_instance.h"
#include "game/experience.h"
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

		GameTime despawnDelay = constants::OneSecond * 30;

		if (controlled.IsTagged())
		{
			uint32 sumLevel = 0;

			GamePlayerS* maxLevelCharacter = nullptr;
			std::vector<std::weak_ptr<GamePlayerS>> lootRecipients;
			controlled.ForEachLootRecipient([&controlled, &lootRecipients, &sumLevel, &maxLevelCharacter](const std::shared_ptr<GamePlayerS>& character)
				{
					const uint32 characterLevel = character->GetLevel();
					sumLevel += characterLevel;

					const uint32 xpCutoffLevel = xp::GetExpCutoffLevel(characterLevel);
					if (controlled.Get<uint32>(object_fields::Level) > xpCutoffLevel)
					{
						if (!maxLevelCharacter || maxLevelCharacter->GetLevel() < characterLevel)
						{
							maxLevelCharacter = character.get();
						}
					}

					lootRecipients.push_back(character);
				});

			// Reward all recipients
			for (const auto& recipient : lootRecipients)
			{
				//recipient->onQuestKillCredit(controlled.GetGuid(), controlled.GetEntry());
			}

			// Reward the killer with experience points
			const float t =
				controlled.GetEntry().maxlevel() != controlled.GetEntry().minlevel() ?
				(controlled.GetLevel() - controlled.GetEntry().minlevel()) / (controlled.GetEntry().maxlevel() - controlled.GetEntry().minlevel()) :
				0.0f;

			// Base XP for equal level
			uint32 xp = Interpolate(controlled.GetEntry().minlevelxp(), controlled.GetEntry().maxlevelxp(), t);
			if (!maxLevelCharacter)
			{
				xp = 0;
			}
			else
			{
				if (controlled.GetLevel() > maxLevelCharacter->GetLevel())
				{
					uint32 levelDiff = controlled.GetLevel() - maxLevelCharacter->GetLevel();
					if (levelDiff > 4) 
					{
						levelDiff = 4;
					}

					xp *= 1 + 0.05f * levelDiff;
				}
				else if(controlled.GetLevel() < maxLevelCharacter->GetLevel())
				{
					const uint32 cutoffLevel = xp::GetExpCutoffLevel(maxLevelCharacter->GetLevel());
					if (controlled.GetLevel() > cutoffLevel)
					{
						const uint32 zd = xp::GetZeroDifference(maxLevelCharacter->GetLevel());
						xp *= 1 - (maxLevelCharacter->GetLevel() - controlled.GetLevel()) / zd;
					}
					else
					{
						xp = 0;
					}
				}
			}

			// Group xp modifier
			const float groupModifier = xp::GetGroupXpRate(lootRecipients.size(), false);
			for (const auto& character : lootRecipients)
			{
				const auto strongCharacter = character.lock();
				if (!strongCharacter)
				{
					continue;
				}

				if (!strongCharacter->IsAlive())
				{
					continue;
				}

				const uint32 cutoffLevel = xp::GetExpCutoffLevel(strongCharacter->GetLevel());
				if (controlled.GetLevel() <= cutoffLevel) {
					continue;
				}

				const float rate = groupModifier * static_cast<float>(strongCharacter->GetLevel()) / sumLevel;
				strongCharacter->RewardExperience(xp * rate);
			}

			const auto* lootEntry = controlled.GetProject().unitLoot.getById(entry.unitlootentry());

			if (lootEntry)
			{
				// Generate loot
				auto loot = std::make_unique<LootInstance>(controlled.GetProject().items, controlled.GetGuid(), lootEntry, lootEntry->minmoney(), lootEntry->maxmoney(), lootRecipients);

				// 3 Minutes of despawn delay if creature still has loot
				despawnDelay = constants::OneMinute * 3;

				// As soon as the loot window is cleared, toggle the flag
				m_onLootCleared = loot->cleared.connect([&controlled]()
					{
						controlled.RemoveFlag<uint32>(object_fields::Flags, unit_flags::Lootable);
					});

				controlled.SetUnitLoot(std::move(loot));
			}

		}

		// Activate despawn timer
		controlled.TriggerDespawnTimer(despawnDelay);
	}

	void CreatureAIDeathState::OnLeave()
	{
		CreatureAIState::OnLeave();
	}
}
