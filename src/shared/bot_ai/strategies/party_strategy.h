// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	class BotAiRegistry;

	/// Distance at which a following bot starts walking after its leader again.
	constexpr float BotFollowDistance = 7.0f;

	/// How close it walks up to them.
	constexpr float BotFollowAcceptance = 3.0f;

	/// Distance at which a following bot breaks off a fight to catch up. A bot that keeps
	/// fighting while its leader runs on is left behind, which is the one thing a party member
	/// must not do.
	constexpr float BotFollowLeashDistance = 30.0f;

	/// How far the leader has to move from where the bot was last sent before it re-paths.
	constexpr float BotFollowRepathDistance = 4.0f;

	/// Minimum time between two re-paths while chasing a moving leader.
	constexpr GameTime BotFollowRepathIntervalMs = 700;

	/// Registers what a bot needs to play in a party: following a leader and fighting what it
	/// fights. Strategies "party_world" (replaces solo grinding out of combat) and "party_combat"
	/// (layered over "grind_combat"). Requires RegisterGrindStrategies for the shared fighting
	/// triggers and actions.
	void RegisterPartyStrategies(BotAiRegistry& registry);
}
