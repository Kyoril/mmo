// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "test_unit_factory.h"
#include "game/spell.h"
#include "asio/io_service.hpp"

#include <algorithm>

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

TEST_CASE("GM cooldown bypass clears existing cooldowns and stops new cooldowns until disabled", "[spell_lockout][gm_cheats]")
{
	asio::io_service io;
	TimerQueue timers(io);
	proto::Project project;
	const auto unit = test::MakeUnit(project, timers);
	const GameTime normalGcd = unit->GetGlobalCooldownDuration();
	unit->SetCooldown(42, 60000);
	unit->SetSpellCategoryCooldown(7, 60000);
	unit->SetGlobalCooldown(60000);
	REQUIRE(unit->SpellHasCooldown(42, 7, 0));

	bool resetNotified = false;
	const auto connection = unit->spellCooldownsReset.connect([&](const std::vector<uint32>& spells)
	{
		resetNotified = true;
		CHECK(std::find(spells.begin(), spells.end(), 42) != spells.end());
	});
	unit->SetNoCooldown(true);
	CHECK(resetNotified);
	CHECK_FALSE(unit->SpellHasCooldown(42, 7, 0));
	CHECK(unit->GetGlobalCooldownDuration() == 0);

	unit->SetCooldown(42, 60000);
	unit->SetSpellCategoryCooldown(7, 60000);
	unit->SetGlobalCooldown(60000);
	CHECK_FALSE(unit->SpellHasCooldown(42, 7, 0));
	unit->SetNoCooldown(false);
	CHECK_FALSE(unit->SpellHasCooldown(42, 7, 0));
	CHECK(unit->GetGlobalCooldownDuration() == normalGcd);

	unit->SetCooldown(42, 60000);
	CHECK(unit->SpellHasCooldown(42, 0, IgnoreGcd));
	unit->SetSpellCategoryCooldown(7, 60000);
	CHECK(unit->SpellHasCooldown(43, 7, IgnoreGcd));
	unit->SetGlobalCooldown(60000);
	CHECK(unit->SpellHasCooldown(43, 0, 0));
}

TEST_CASE("GM cooldown bypass suppresses deferred cooldown notifications", "[spell_lockout][gm_cheats]")
{
	asio::io_service io;
	TimerQueue timers(io);
	proto::Project project;
	const auto unit = test::MakeUnit(project, timers);
	auto spell = test::MakeSpell();
	spell.set_id(42);
	spell.set_cooldown(60000);
	spell.set_category(7);
	spell.set_categorycooldown(60000);
	uint32 notifications = 0;
	const auto connection = unit->spellCooldownStarted.connect([&](uint32, GameTime) { ++notifications; });
	unit->SetNoCooldown(true);
	unit->StartDeferredSpellCooldown(spell);
	CHECK(notifications == 0);
	CHECK_FALSE(unit->SpellHasCooldown(42, 7, IgnoreGcd));
	unit->SetNoCooldown(false);
	unit->StartDeferredSpellCooldown(spell);
	CHECK(notifications == 1);
	CHECK(unit->SpellHasCooldown(42, 7, IgnoreGcd));
}
