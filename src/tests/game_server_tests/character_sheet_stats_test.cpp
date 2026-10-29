// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "test_unit_factory.h"

#include "game/object_type_id.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

using namespace mmo;
using namespace mmo::test;

// The character sheet shows caster and healer values next to the melee ones (spell damage, spell
// healing, spell crit chance, melee crit chance and mana regeneration). None of them were
// replicated, so the client had nothing to show. They are derived server side and mirrored into
// replicated unit fields by GamePlayerS::RefreshStats(), which runs on every stat, level,
// equipment and aura change.

TEST_CASE("Character sheet combat stat fields are replicated after a stat refresh", "[character_sheet_stats]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeUnit(project, timers);

	// Base crit chance (5%) plus the agility contribution (20 agility = 1%). The test class gives
	// 10 agility per level base value, so this is 5 + 0.5.
	CHECK(unit->Get<float>(object_fields::MeleeCritChance) == Approx(5.5f));

	// The configured default spell crit chance; per-spell talent modifiers are applied at cast time.
	CHECK(unit->Get<float>(object_fields::SpellCritChance) == Approx(project.combatSettings.spell_default_crit_chance()));

	// basemanaregenpertick is 20 and regeneration ticks every two seconds, so five seconds
	// restore 2.5 ticks worth of mana.
	CHECK(unit->Get<float>(object_fields::ManaRegen) == Approx(50.0f));

	// Nothing grants spell power or healing power yet.
	CHECK(unit->Get<float>(object_fields::SpellDamage) == Approx(0.0f));
	CHECK(unit->Get<float>(object_fields::SpellHealing) == Approx(0.0f));
}

TEST_CASE("Spell power and healing power modifiers reach the replicated fields", "[character_sheet_stats]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeUnit(project, timers);

	// This is the path an equipped "+N spell damage" item takes: its on-equip aura updates the
	// unit modifier, which refreshes the stats.
	unit->UpdateModifierValue(unit_mods::SpellDamage, unit_mod_type::TotalValue, 26.0f, true);
	CHECK(unit->Get<float>(object_fields::SpellDamage) == Approx(26.0f));

	unit->UpdateModifierValue(unit_mods::HealingDone, unit_mod_type::TotalValue, 14.0f, true);
	CHECK(unit->Get<float>(object_fields::SpellHealing) == Approx(14.0f));

	// Unequipping has to take the bonus away again, not leave it on the character sheet.
	unit->UpdateModifierValue(unit_mods::SpellDamage, unit_mod_type::TotalValue, 26.0f, false);
	CHECK(unit->Get<float>(object_fields::SpellDamage) == Approx(0.0f));
}

TEST_CASE("Melee crit chance field tracks the agility the character has", "[character_sheet_stats]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeUnit(project, timers);

	// 40 additional agility are worth 2% crit on top of the base chance.
	unit->UpdateModifierValue(GameUnitS::GetUnitModByStat(2), unit_mod_type::TotalValue, 40.0f, true);	// Agility
	CHECK(unit->Get<float>(object_fields::MeleeCritChance) == Approx(7.5f));
	CHECK(unit->GetBaseMeleeCritChance() == Approx(7.5f));
}
