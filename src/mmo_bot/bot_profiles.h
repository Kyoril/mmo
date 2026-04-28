// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

// Convenience header that includes all bot profiles

#include "bot_profile.h"
#include "bot_profiles/simple_greeter_profile.h"
#include "bot_profiles/chatter_profile.h"
#include "bot_profiles/patrol_profile.h"
#include "bot_profiles/sequence_profile.h"
#include "bot_profiles/unit_awareness_profile.h"
#include "bot_profiles/combat_profile.h"
#include "bot_startup_types.h"

#include <string_view>
#include <vector>

using namespace std::chrono_literals;

namespace mmo
{
	inline std::vector<StartupProfileEntry> GetStartupProfileCatalog()
	{
		return {
			{ "simple_greeter" },
			{ "chatter" },
			{ "sequence" },
			{ "unit_awareness" },
			{ "combat" },
		};
	}

	inline BotProfilePtr CreateBotProfile(std::string_view key, const BotConfig& config)
	{
		if (key == "simple_greeter")
		{
			return std::make_shared<SimpleGreeterProfile>(config.greeting);
		}
		if (key == "chatter")
		{
			return std::make_shared<ChatterProfile>(
				std::vector<std::string>{
					"Hello!",
					"How is everyone doing?",
					"I'm a test bot!",
					"This is pretty cool!",
				},
				3000ms);
		}
		if (key == "sequence")
		{
			return std::make_shared<SequenceProfile>();
		}
		if (key == "unit_awareness")
		{
			return std::make_shared<UnitAwarenessProfile>();
		}
		if (key == "combat")
		{
			return std::make_shared<CombatProfile>();
		}

		return nullptr;
	}
}
