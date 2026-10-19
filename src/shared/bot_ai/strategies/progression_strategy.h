// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

namespace mmo
{
	class BotAiRegistry;

	/// Registers character progression out of combat: spending talent points along the bot's
	/// build (data/bots/talent_builds.json). Strategy "progression_world".
	void RegisterProgressionStrategies(BotAiRegistry& registry);
}
