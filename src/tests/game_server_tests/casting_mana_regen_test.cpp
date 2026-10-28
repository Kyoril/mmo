// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "test_unit_factory.h"

#include "game_server/spells/aura_effect.h"
#include "game_server/spells/aura_container.h"
#include "game/aura.h"
#include "game/object_type_id.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

using namespace mmo;
using namespace mmo::test;

// Within five seconds after spending mana a unit no longer stops regenerating mana entirely: a
// share of its normal regeneration (combat_settings.casting_mana_regen_percent, 30% by default)
// keeps flowing, and ModCastingManaRegenPercent auras raise that share for talents and buffs.

TEST_CASE("Casting mana regeneration defaults to 30 percent of normal regeneration", "[casting_mana_regen]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeUnit(project, timers);

	CHECK(unit->GetCastingManaRegenPercent() == Approx(30.0f));

	// basemanaregenpertick is 20, so casting regenerates 6 per tick and 15 per five seconds,
	// next to the 50 per five seconds of normal regeneration.
	CHECK(unit->GetCastingManaRegenPerTick() == Approx(6.0f));
	CHECK(unit->Get<float>(object_fields::ManaRegen) == Approx(50.0f));
	CHECK(unit->Get<float>(object_fields::ManaRegenCasting) == Approx(15.0f));
}

TEST_CASE("The combat setting changes the casting regeneration share", "[casting_mana_regen]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	project.combatSettings.set_casting_mana_regen_percent(50.0f);
	auto unit = MakeUnit(project, timers);

	CHECK(unit->GetCastingManaRegenPercent() == Approx(50.0f));
	CHECK(unit->Get<float>(object_fields::ManaRegenCasting) == Approx(25.0f));
}

TEST_CASE("ModCastingManaRegenPercent raises the share and reaches the character sheet", "[casting_mana_regen]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeUnit(project, timers);

	const proto::SpellEntry spell = MakeSpell();
	AuraContainer container(*unit, /*casterId=*/0, spell, /*duration=*/0, /*itemGuid=*/0);

	proto::SpellEffect effect;
	effect.set_aura(static_cast<uint32>(aura_type::ModCastingManaRegenPercent));
	AuraEffect aura(container, effect, timers, /*basePoints=*/20);

	aura.HandleEffect(true);
	CHECK(unit->GetCastingManaRegenPercent() == Approx(50.0f));
	CHECK(unit->Get<float>(object_fields::ManaRegenCasting) == Approx(25.0f));

	aura.HandleEffect(false);
	CHECK(unit->GetCastingManaRegenPercent() == Approx(30.0f));
	CHECK(unit->Get<float>(object_fields::ManaRegenCasting) == Approx(15.0f));
}

TEST_CASE("The casting regeneration share is clamped to 0..100 percent", "[casting_mana_regen]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeUnit(project, timers);

	// Casting can never regenerate more than not casting.
	unit->ModifyCastingManaRegenPercentBonus(500.0f, true);
	CHECK(unit->GetCastingManaRegenPercent() == Approx(100.0f));
	CHECK(unit->GetCastingManaRegenPerTick() == Approx(unit->GetManaRegenPerTick()));
	unit->ModifyCastingManaRegenPercentBonus(500.0f, false);

	// Nor can a negative modifier drain mana while casting.
	unit->ModifyCastingManaRegenPercentBonus(-80.0f, true);
	CHECK(unit->GetCastingManaRegenPercent() == Approx(0.0f));
	CHECK(unit->GetCastingManaRegenPerTick() == Approx(0.0f));
}

TEST_CASE("ModPowerRegenPercent also scales the casting value on the character sheet", "[casting_mana_regen]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeUnit(project, timers);

	const proto::SpellEntry spell = MakeSpell();
	AuraContainer container(*unit, /*casterId=*/0, spell, /*duration=*/0, /*itemGuid=*/0);

	proto::SpellEffect effect;
	effect.set_aura(static_cast<uint32>(aura_type::ModPowerRegenPercent));
	effect.set_miscvaluea(static_cast<int32>(power_type::Mana));
	AuraEffect aura(container, effect, timers, /*basePoints=*/100);

	// The handler refreshes the replicated fields itself; the regen scalar is not part of
	// RefreshStats().
	aura.HandleEffect(true);
	CHECK(unit->Get<float>(object_fields::ManaRegen) == Approx(100.0f));
	CHECK(unit->Get<float>(object_fields::ManaRegenCasting) == Approx(30.0f));
}
