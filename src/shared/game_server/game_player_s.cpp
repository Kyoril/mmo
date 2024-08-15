// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "game_player_s.h"

#include "proto_data/project.h"

namespace mmo
{
	GamePlayerS::GamePlayerS(const proto::Project& project, TimerQueue& timerQueue)
		: GameUnitS(project, timerQueue)
	{
	}

	void GamePlayerS::Initialize()
	{
		GameUnitS::Initialize();

		// Initialize some default values
		Set<int32>(object_fields::MaxLevel, 5, false);
		Set<int32>(object_fields::Xp, 0, false);
		Set<int32>(object_fields::NextLevelXp, 400, false);
		Set<int32>(object_fields::Level, 1, false);
	}

	void GamePlayerS::SetClass(const proto::ClassEntry& classEntry)
	{
		m_classEntry = &classEntry;

		Set<int32>(object_fields::MaxLevel, classEntry.levelbasevalues_size());
		Set<int32>(object_fields::PowerType, classEntry.powertype());
	}

	void GamePlayerS::RewardExperience(const uint32 xp)
	{
		// At max level we can't gain any more xp
		if (Get<uint32>(object_fields::Level) >= Get<uint32>(object_fields::MaxLevel))
		{
			return;
		}

		uint32 currentXp = Get<uint32>(object_fields::Xp);
		currentXp += xp;

		// Levelup as often as required
		while(currentXp > Get<uint32>(object_fields::NextLevelXp))
		{
			currentXp -= Get<uint32>(object_fields::NextLevelXp);
			SetLevel(Get<uint32>(object_fields::Level) + 1);
		}

		// Store remaining xp after potential levelups
		Set<uint32>(object_fields::Xp, currentXp);

		// Send packet

	}

	void GamePlayerS::RefreshStats()
	{
		ASSERT(m_classEntry);

		GameUnitS::RefreshStats();

		const int32 level = Get<int32>(object_fields::Level);
		ASSERT(level > 0);
		ASSERT(level <= m_classEntry->levelbasevalues_size());

		// Adjust stats
		const auto* levelStats = &m_classEntry->levelbasevalues(level - 1);
		Set<uint32>(object_fields::StatStamina, levelStats->stamina());
		Set<uint32>(object_fields::StatStrength, levelStats->strength());
		Set<uint32>(object_fields::StatAgility, levelStats->agility());
		Set<uint32>(object_fields::StatIntellect, levelStats->intellect());
		Set<uint32>(object_fields::StatSpirit, levelStats->spirit());

		// TODO: Apply item stats
		
		// Calculate max health from stats
		uint32 maxHealth = UnitStats::GetMaxHealthFromStamina(Get<uint32>(object_fields::StatStamina));
		maxHealth += levelStats->health();
		Set<uint32>(object_fields::MaxHealth, maxHealth);

		// Ensure health is properly capped by max health
		if (Get<uint32>(object_fields::Health) > maxHealth)
		{
			Set<uint32>(object_fields::Health, maxHealth);
		}

		uint32 maxMana = UnitStats::GetMaxManaFromIntellect(Get<uint32>(object_fields::StatIntellect));
		maxMana += levelStats->mana();
		Set<uint32>(object_fields::MaxMana, maxMana);

		// Ensure mana is properly capped by max health
		if (Get<uint32>(object_fields::Mana) > maxMana)
		{
			Set<uint32>(object_fields::Mana, maxMana);
		}

		// TODO
		Set<float>(object_fields::MinDamage, 8.0f);
		Set<float>(object_fields::MaxDamage, 12.0f);
	}

	void GamePlayerS::SetLevel(uint32 newLevel)
	{
		// Anything to do?
		if (newLevel == Get<uint32>(object_fields::Level) || newLevel == 0)
		{
			return;
		}

		// Exceed max level?
		if (newLevel > Get<int32>(object_fields::MaxLevel))
		{
			newLevel = Get<int32>(object_fields::MaxLevel);
		}

		GameUnitS::SetLevel(newLevel);

		// Update next level xp
		Set<uint32>(object_fields::NextLevelXp, 400 * newLevel);
	}
}
