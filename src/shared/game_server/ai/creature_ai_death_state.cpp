// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/ai/creature_ai_death_state.h"

#include <algorithm>
#include "game_server/ai/creature_ai.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/loot_instance.h"
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

		controlled.RaiseTrigger(trigger_event::OnKilled);

		const proto::UnitEntry& entry = controlled.GetEntry();

		GameTime despawnDelay = constants::OneSecond * 30;

		if (controlled.IsTagged())
		{
			uint32 sumLevel = 0;

			GamePlayerS* maxLevelCharacter = nullptr;
			std::map<uint64, std::shared_ptr<GamePlayerS>> lootRecipients;
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

					lootRecipients[character->GetGuid()] = character;
				});

			// Now for each loot recipient, check their groups as well and add nearby members as loot recipients
			for (const auto& recipient : lootRecipients)
			{
				if (auto groupId = recipient.second->GetGroupId(); groupId != 0)
				{
					const Vector3 location(controlled.GetPosition());

					// Find nearby group members and make them loot recipients, too
					controlled.GetWorldInstance()->GetUnitFinder().FindUnits(Circle(location.x, location.z, 100.0f), [&lootRecipients, &controlled, groupId](GameUnitS& unit) -> bool
						{
							if (!unit.IsPlayer())
							{
								return true;
							}

							if (lootRecipients.contains(unit.GetGuid()))
							{
								return true;
							}

							// Check characters group
							const auto player = std::static_pointer_cast<GamePlayerS>(unit.shared_from_this());
							if (player->GetGroupId() == groupId)
							{
								lootRecipients[player->GetGuid()] = player;
							}

							return true;
						});
				}
			}

			// Reward all recipients
			for (const auto& recipient : lootRecipients)
			{
				recipient.second->OnQuestKillCredit(controlled.GetGuid(), controlled.GetEntry());
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
					float levelDiff = controlled.GetLevel() - maxLevelCharacter->GetLevel();
					levelDiff = std::min<float>(levelDiff, 4);

					const float factor = 1.0f + 0.05f * levelDiff;
					xp *= factor;
				}
				else if(controlled.GetLevel() < maxLevelCharacter->GetLevel())
				{
					const uint32 cutoffLevel = xp::GetExpCutoffLevel(maxLevelCharacter->GetLevel());
					if (controlled.GetLevel() > cutoffLevel)
					{
						const float zd = static_cast<float>(xp::GetZeroDifference(maxLevelCharacter->GetLevel()));
						const float factor = 1.0f - static_cast<float>(maxLevelCharacter->GetLevel() - controlled.GetLevel()) / zd;
						xp *= factor;
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
				if (!character.second->IsAlive())
				{
					continue;
				}

				const uint32 cutoffLevel = xp::GetExpCutoffLevel(character.second->GetLevel());
				if (controlled.GetLevel() <= cutoffLevel) {
					continue;
				}

				const float rate = groupModifier * static_cast<float>(character.second->GetLevel()) / sumLevel;
				character.second->RewardExperience(xp * rate);
			}

			const auto* lootEntry = controlled.GetProject().unitLoot.getById(entry.unitlootentry());

			if (lootEntry)
			{
				// Generate loot
				std::vector<std::weak_ptr<GamePlayerS>> weakRecipients;
				for (const auto& recipient : lootRecipients)
				{
					weakRecipients.push_back(recipient.second);
				}

				auto loot = std::make_unique<LootInstance>(controlled.GetProject().items, controlled.GetGuid(), lootEntry, lootEntry->minmoney(), lootEntry->maxmoney(), weakRecipients);

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
