// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Glide stats of glider items and the effective glide settings they produce.

#include "catch.hpp"

#include "game/glide_stats.h"
#include "game/item.h"

using namespace mmo;

TEST_CASE("Glide stats scale the base glide settings", "[glide_stats]")
{
	GlideModifiers modifiers;
	modifiers.speedPercent = 20;
	modifiers.agilityPercent = 50;
	modifiers.liftPercent = 40;
	modifiers.endurancePercent = 25;

	const TraversalSettings result = ApplyGlideModifiers(TraversalSettings(), modifiers);
	CHECK(result.glideForwardSpeed == Approx(12.0f));
	CHECK(result.glideTurnRate == Approx(4.5f));
	CHECK(result.glideSinkSpeed == Approx(1.5f));
	CHECK(result.glideDrainPerSecond == Approx(6.0f));
	CHECK(result.glideDrainInUpdraftPerSecond == Approx(3.0f));
	CHECK(result.maxVigor == Approx(100.0f));
	CHECK(result.glideStartCost == Approx(5.0f));
}

TEST_CASE("Glide stats are clamped to their caps", "[glide_stats]")
{
	GlideModifiers high;
	high.speedPercent = 999;
	high.agilityPercent = 999;
	high.liftPercent = 999;
	high.endurancePercent = 999;

	const TraversalSettings capped = ApplyGlideModifiers(TraversalSettings(), high);
	CHECK(capped.glideForwardSpeed == Approx(15.0f));
	CHECK(capped.glideTurnRate == Approx(6.0f));
	CHECK(capped.glideSinkSpeed == Approx(1.0f));
	CHECK(capped.glideDrainPerSecond == Approx(2.0f));

	GlideModifiers negative;
	negative.speedPercent = -50;
	negative.agilityPercent = -50;
	negative.liftPercent = -50;
	negative.endurancePercent = -50;

	const TraversalSettings base;
	const TraversalSettings clamped = ApplyGlideModifiers(base, negative);
	CHECK(clamped.glideForwardSpeed == Approx(base.glideForwardSpeed));
	CHECK(clamped.glideTurnRate == Approx(base.glideTurnRate));
	CHECK(clamped.glideSinkSpeed == Approx(base.glideSinkSpeed));
	CHECK(clamped.glideDrainPerSecond == Approx(base.glideDrainPerSecond));
}

TEST_CASE("Only glide stat types add to the glide modifiers", "[glide_stats]")
{
	GlideModifiers modifiers;
	AddGlideStat(modifiers, item_stat::Mana, 10);
	AddGlideStat(modifiers, item_stat::FromAttribute(0), 10);
	AddGlideStat(modifiers, 31, 10);
	CHECK(modifiers.speedPercent == 0);
	CHECK(modifiers.agilityPercent == 0);
	CHECK(modifiers.liftPercent == 0);
	CHECK(modifiers.endurancePercent == 0);

	AddGlideStat(modifiers, item_stat::GlideSpeed, 10);
	AddGlideStat(modifiers, item_stat::GlideAgility, 10);
	AddGlideStat(modifiers, item_stat::GlideLift, 10);
	AddGlideStat(modifiers, item_stat::GlideEndurance, 10);
	CHECK(modifiers.speedPercent == 10);
	CHECK(modifiers.agilityPercent == 10);
	CHECK(modifiers.liftPercent == 10);
	CHECK(modifiers.endurancePercent == 10);
}

TEST_CASE("Glider item data yields its glide modifiers", "[glide_stats]")
{
	ItemInfo item{};
	item.stats[0] = { static_cast<uint8>(item_stat::GlideSpeed), 15 };
	item.stats[1] = { static_cast<uint8>(item_stat::Health), 30 };

	const GlideModifiers modifiers = GetGlideModifiers(item);
	CHECK(modifiers.speedPercent == 15);
	CHECK(modifiers.agilityPercent == 0);
	CHECK(modifiers.liftPercent == 0);
	CHECK(modifiers.endurancePercent == 0);
}

TEST_CASE("Glide stat types have localization keys", "[glide_stats]")
{
	ItemInfo item{};
	item.stats[0] = { static_cast<uint8>(item_stat::GlideSpeed), 20 };
	item.stats[1] = { static_cast<uint8>(item_stat::GlideEndurance), 20 };

	CHECK(std::string(item.GetStatType(0)) == "GLIDE_SPEED");
	CHECK(std::string(item.GetStatType(1)) == "GLIDE_ENDURANCE");
}
