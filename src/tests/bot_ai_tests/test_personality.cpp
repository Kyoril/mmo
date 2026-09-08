// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "bot_ai/bot_personality.h"
#include "bot_ai/strategies/grind_actions.h"
#include "bot_ai/strategies/grind_triggers.h"

#include <cmath>
#include <set>
#include <vector>

using namespace mmo;

namespace
{
	/// The seed the swarm derives for a bot from its index, kept in step with BotAiContext.
	uint32 SeedForBot(const uint32 index)
	{
		return 0x9e3779b9u ^ (index * 2654435761u);
	}
}

TEST_CASE("Neighbouring bots get genuinely different traits", "[bot_ai][personality]")
{
	// The whole point of the personality: bots share a spawn, a level band and a spawn table, so
	// identical traits make identical choices - they pick the same creature, walk to the same
	// coordinate and stand inside one another.
	std::set<int> angles;
	std::set<int> radii;
	std::set<std::size_t> choices;

	for (uint32 index = 0; index < 20; ++index)
	{
		const BotPersonality personality = BotPersonality::FromSeed(SeedForBot(index));

		// Bucketed finely: coarse buckets would count collisions in the bucketing rather than
		// agreement between the bots.
		angles.insert(static_cast<int>(personality.approachAngle * 100.0f));
		radii.insert(static_cast<int>(personality.searchRadiusScale * 100.0f));
		choices.insert(personality.candidateChoices);
	}

	CHECK(angles.size() >= 18);
	CHECK(radii.size() >= 18);
	CHECK(choices.size() >= 4);
}

TEST_CASE("A bot keeps the same personality across restarts", "[bot_ai][personality]")
{
	// Reproducibility is what lets one swarm run be compared with the next.
	const BotPersonality first = BotPersonality::FromSeed(SeedForBot(7));
	const BotPersonality second = BotPersonality::FromSeed(SeedForBot(7));

	CHECK(first.approachAngle == Approx(second.approachAngle));
	CHECK(first.searchRadiusScale == Approx(second.searchRadiusScale));
	CHECK(first.restHealthFraction == Approx(second.restHealthFraction));
}

TEST_CASE("Traits stay inside ranges the strategy can use", "[bot_ai][personality]")
{
	for (uint32 index = 0; index < 200; ++index)
	{
		const BotPersonality personality = BotPersonality::FromSeed(SeedForBot(index));

		INFO("bot " << index);

		CHECK(personality.candidateChoices >= 1);
		CHECK(personality.searchRadiusScale > 0.0f);

		// The offset plus the tolerance the bot may stop within has to stay inside melee range,
		// or it arrives at its chosen spot, is told it is still out of range, and walks in again
		// forever. That loop cost five approaches per swing before the tolerance was tightened.
		CHECK(personality.combatOffset + BotCombatStandAcceptance < BotMeleeRange);

		// A bot must always want to recover further than the point it broke off at, or it
		// rejoins the fight on the hit that drove it out.
		CHECK(personality.restedHealthFraction > personality.restHealthFraction);
		CHECK(personality.restedHealthFraction <= 0.95f);
		CHECK(personality.restHealthFraction > 0.0f);
	}
}

TEST_CASE("Offsets are placed around a point without changing its height", "[bot_ai][personality]")
{
	const Vector3 center(10.0f, 5.0f, -20.0f);
	const Vector3 offset = OffsetAround(center, 0.0f, 4.0f);

	CHECK(offset.x == Approx(14.0f));
	CHECK(offset.z == Approx(-20.0f));

	// Height is carried through unchanged: moving the destination up or down would put it
	// through a floor, which is the bug that made bots walk into cellars.
	CHECK(offset.y == Approx(5.0f));

	const Vector3 quarter = OffsetAround(center, 1.5707963f, 4.0f);
	CHECK(quarter.x == Approx(10.0f).margin(0.001));
	CHECK(quarter.z == Approx(-16.0f));
	CHECK(quarter.y == Approx(5.0f));
}
