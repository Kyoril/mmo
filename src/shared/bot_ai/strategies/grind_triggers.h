// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_ai/bot_trigger.h"

namespace mmo
{
	class BotAiRegistry;

	/// Distance at which the bot considers itself in melee range of its target.
	///
	/// The server computes this from both units' combat reach, which the bot does not replicate.
	/// Under-estimating is the safe direction: standing slightly too close costs nothing, while
	/// standing slightly too far makes every swing fail with an out-of-range error.
	constexpr float BotMeleeRange = 3.0f;

	/// How close the bot has to get to a grind spot before it stops travelling and starts
	/// looking around. Well inside the range at which the server keeps units visible, so that
	/// the object manager already knows about the creatures when the bot arrives.
	constexpr float BotGrindSpotArrivalRange = 15.0f;

	/// And how much height may separate the bot from the spot while still counting as arrived.
	/// The arrival test is horizontal, so without this a spawn in a cellar counts as reached from
	/// the floor above it - the bot stops travelling, decides it is there, and then tries to walk
	/// to creatures on the other side of a floor.
	constexpr float BotGrindSpotArrivalHeight = 5.0f;

	/// Health and power are fractions in [0, 1] throughout, matching BotPerception.
	///
	/// Two thresholds rather than one, because a single one makes a bot flicker: it would stop
	/// resting the instant it crossed the line and start again on the next hit it took. The bot
	/// breaks off below the rest thresholds and does not fight again until it is back above the
	/// rested ones.
	constexpr float BotRestHealthFraction = 0.4f;
	constexpr float BotRestPowerFraction = 0.2f;

	constexpr float BotRestedHealthFraction = 0.9f;
	constexpr float BotRestedPowerFraction = 0.8f;

	/// Registers every trigger the grind strategy names.
	void RegisterGrindTriggers(BotAiRegistry& registry);
}
