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
	}

	void GamePlayerS::SetLevel(uint32 newLevel)
	{
		// Anything to do?
		if (newLevel == Get<uint32>(object_fields::Level))
		{
			return;
		}

		// Exceed max level?
		if (newLevel > Get<int32>(object_fields::MaxLevel))
		{
			return;
		}

		GameUnitS::SetLevel(newLevel);

		// Update next level xp
		Set<uint32>(object_fields::NextLevelXp, 400 * newLevel);
	}
}
