// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	class BotAiRegistry;

	/// How far a bot will walk to reach a grind spot. Beyond this it is cheaper to look for
	/// somewhere else than to spend minutes travelling.
	constexpr float BotGrindSearchRadius = 600.0f;

	/// Levels below and above the bot that a grind spot may be. Asymmetric on purpose: a
	/// creature a few levels under the bot is easy experience, one above it is a fair fight, and
	/// two above it is a death.
	constexpr uint32 BotGrindLevelsBelow = 3;
	constexpr uint32 BotGrindLevelsAbove = 1;

	/// Nearest candidates a bot picks from at random. Always taking the closest one would pile
	/// every bot in a region onto the same spawn.
	constexpr std::size_t BotGrindCandidateChoices = 5;

	/// How long a spot the navigation mesh could not reach is left alone. Spawn positions come
	/// out of the proto project, and being in the data does not mean being reachable.
	constexpr GameTime BotGrindBlacklistMs = 300000;

	/// How long a bot waits at a spot with nothing alive on it before giving up on it. A spawn
	/// point in the data says where a creature lives, not whether one is standing there now.
	constexpr GameTime BotGrindBarrenTimeoutMs = 8000;

	/// A barren spot is worth trying again much sooner than an unreachable one: the creatures on
	/// it are almost certainly just respawning.
	constexpr GameTime BotGrindBarrenBlacklistMs = 60000;

	/// Registers every action the grind strategy names.
	void RegisterGrindActions(BotAiRegistry& registry);
}
