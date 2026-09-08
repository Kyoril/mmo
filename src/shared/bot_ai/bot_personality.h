// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

#include <cstddef>

namespace mmo
{
	/// The traits that make one bot behave unlike the next.
	///
	/// Derived once from the bot's seed and then fixed, rather than rolled per decision. A bot
	/// that re-rolls how far it is willing to travel every tick does not read as a character with
	/// preferences, it reads as a twitch; and a stable personality means a swarm run can be
	/// compared against the one before it.
	///
	/// Every field exists because without it the whole population did the same thing at the same
	/// time. Bots share a spawn point, a level band and a spawn table, so identical rules make
	/// identical choices - they picked the same creature, walked to the same coordinate, and
	/// ended up standing inside one another.
	struct BotPersonality final
	{
		/// Scales how far this bot will travel for somewhere to fight. A wanderer ranges over
		/// several times the area a homebody does, which is what spreads a population out across
		/// a zone instead of piling it onto the nearest spawn.
		float searchRadiusScale { 1.0f };

		/// How many of the nearest candidate spots this bot picks among. A bot that considers
		/// more of them is less likely to want what its neighbour wants.
		std::size_t candidateChoices { 5 };

		/// The direction this bot approaches things from, in radians. Fixed per bot, so a group
		/// converging on one creature arranges itself around it rather than in one place.
		float approachAngle { 0.0f };

		/// How far off the exact spawn point this bot stands while working a spot.
		float spotOffset { 4.0f };

		/// How far from a creature this bot stops. Kept well inside melee range: this offset plus
		/// the tolerance the bot is allowed to stop within has to stay under it, or the bot arrives
		/// at its chosen spot, is told it is still out of range, and walks in again forever.
		float combatOffset { 1.4f };

		/// Health fraction this bot breaks off at, and the one it returns to the fight at. A
		/// cautious bot disengages early and rests longer; a reckless one fights on.
		float restHealthFraction { 0.4f };
		float restedHealthFraction { 0.9f };

		/// Builds the traits for a bot from its stable seed.
		[[nodiscard]] static BotPersonality FromSeed(uint32 seed);
	};

	/// A point at the given angle and distance around a centre, in the horizontal plane.
	/// Y is up in this engine, so the offset is applied to x and z and the height is kept.
	[[nodiscard]] Vector3 OffsetAround(const Vector3& center, float angleRadians, float radius);
}
