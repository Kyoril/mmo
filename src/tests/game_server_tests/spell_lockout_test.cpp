// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "test_unit_factory.h"
#include "game/spell.h"
#include "asio/io_service.hpp"

using namespace mmo;

namespace
{
	// Keeps the global cooldown out of the way so only the cooldown under test is observed.
	constexpr uint32 IgnoreGcd = spell_cooldown_flags::NoGlobalCooldown;
}

TEST_CASE("Category 0 never puts uncategorised spells on a shared cooldown", "[spell_lockout]")
{
	// Interrupting a spell used to apply its category cooldown. Almost every spell has category 0,
	// so a single interrupt locked the target out of nearly everything for the lockout duration.
	asio::io_service io;
	TimerQueue timers(io);
	proto::Project project;
	const auto unit = test::MakeUnit(project, timers);

	unit->SetSpellCategoryCooldown(0, 4000);

	CHECK_FALSE(unit->SpellHasCooldown(42, 0, IgnoreGcd));
}

TEST_CASE("A real spell category still shares its cooldown", "[spell_lockout]")
{
	asio::io_service io;
	TimerQueue timers(io);
	proto::Project project;
	const auto unit = test::MakeUnit(project, timers);

	unit->SetSpellCategoryCooldown(7, 4000);

	CHECK(unit->SpellHasCooldown(42, 7, IgnoreGcd));
	CHECK_FALSE(unit->SpellHasCooldown(42, 8, IgnoreGcd));
}

TEST_CASE("A school lockout only locks the interrupted school", "[spell_lockout]")
{
	asio::io_service io;
	TimerQueue timers(io);
	proto::Project project;
	const auto unit = test::MakeUnit(project, timers);

	unit->LockSpellSchool(spell_school::Frost, 4000);

	CHECK(unit->IsSpellSchoolLocked(spell_school::Frost));
	CHECK_FALSE(unit->IsSpellSchoolLocked(spell_school::Fire));
	CHECK_FALSE(unit->IsSpellSchoolLocked(spell_school::Arcane));
}

TEST_CASE("A school lockout can be lifted", "[spell_lockout]")
{
	asio::io_service io;
	TimerQueue timers(io);
	proto::Project project;
	const auto unit = test::MakeUnit(project, timers);

	SECTION("explicitly with a zero duration")
	{
		unit->LockSpellSchool(spell_school::Fire, 4000);
		unit->LockSpellSchool(spell_school::Fire, 0);
		CHECK_FALSE(unit->IsSpellSchoolLocked(spell_school::Fire));
	}

	SECTION("by a cooldown reset")
	{
		unit->LockSpellSchool(spell_school::Fire, 4000);
		unit->ResetCooldowns();
		CHECK_FALSE(unit->IsSpellSchoolLocked(spell_school::Fire));
	}
}
