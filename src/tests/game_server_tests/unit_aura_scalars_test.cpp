// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "test_unit_factory.h"

#include "game_server/spells/aura_effect.h"
#include "game_server/spells/aura_container.h"
#include "game/aura.h"
#include "game/spell.h"
#include "game/item.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

using namespace mmo;
using namespace mmo::test;

TEST_CASE("ModHealthRegenPercent accumulates and unwinds exactly", "[aura_scalars]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeUnit(project, timers);

	// healthregenpertick is 10 and spiritperhealthregen is unset, so the base is exactly 10.
	CHECK(unit->GetEffectiveHealthRegenPerTick() == Approx(10.0f));

	unit->ModifyHealthRegenPercentBonus(50.0f, true);
	CHECK(unit->GetEffectiveHealthRegenPerTick() == Approx(15.0f));

	// A second aura of the same kind stacks additively.
	unit->ModifyHealthRegenPercentBonus(50.0f, true);
	CHECK(unit->GetEffectiveHealthRegenPerTick() == Approx(20.0f));

	// Misapplying both must land exactly back on the base, not merely near it: an aura that
	// leaks a fraction of its bonus on removal is a permanent stat gain per application.
	unit->ModifyHealthRegenPercentBonus(50.0f, false);
	unit->ModifyHealthRegenPercentBonus(50.0f, false);
	CHECK(unit->GetEffectiveHealthRegenPerTick() == Approx(10.0f));
}

TEST_CASE("RefreshStats does not accumulate flat regeneration across repeated calls", "[aura_scalars]")
{
	// Regression test for a RefreshStats() bug: m_healthRegenPerTick (and m_manaRegenPerTick,
	// fixed by the same edit) were never reset to zero before being recomputed. The spirit-
	// derived term only resets it with an absolute assignment when spiritPerHealthRegen is
	// non-zero; when it is zero (as here), nothing resets the accumulator and the flat
	// healthRegenPerTick term is added again on every RefreshStats() call. RefreshStats runs on
	// every SetClass()/SetLevel() call - i.e. on login, on level up, and on every stat or
	// equipment change (see world_server/realm_connector.cpp, which calls SetClass() then
	// SetLevel() on every character load) - so a fresh unit already carries two calls by the
	// time MakeUnit returns it, and a third one here must not move the result any further.
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeUnit(project, timers, 5);

	const float healthRegenAfterConstruction = unit->GetEffectiveHealthRegenPerTick();
	CHECK(healthRegenAfterConstruction == Approx(10.0f));

	const float manaRegenAfterConstruction = unit->GetManaRegenPerTick();
	CHECK(manaRegenAfterConstruction == Approx(20.0f));

	// A further stat refresh - same level, so nothing should actually change.
	unit->SetLevel(5);

	CHECK(unit->GetEffectiveHealthRegenPerTick() == Approx(healthRegenAfterConstruction));
	CHECK(unit->GetEffectiveHealthRegenPerTick() == Approx(10.0f));

	CHECK(unit->GetManaRegenPerTick() == Approx(manaRegenAfterConstruction));
	CHECK(unit->GetManaRegenPerTick() == Approx(20.0f));
}

TEST_CASE("ModPowerRegenPercent scales only the power type it names", "[aura_scalars]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeUnit(project, timers);

	unit->ModifyPowerRegenPercentBonus(power_type::Mana, 50.0f, true);

	CHECK(unit->GetEffectivePowerRegenPerTick(power_type::Mana, 20) == 30);
	CHECK(unit->GetEffectivePowerRegenPerTick(power_type::Energy, 20) == 20);

	unit->ModifyPowerRegenPercentBonus(power_type::Mana, 50.0f, false);
	CHECK(unit->GetEffectivePowerRegenPerTick(power_type::Mana, 20) == 20);
}

TEST_CASE("A regeneration bonus never accelerates rage decay", "[aura_scalars]")
{
	// Rage regenerates by *decaying* (amount -= 3). Scaling a negative amount would make a
	// "+50% regeneration" buff drain rage half again as fast.
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeUnit(project, timers);

	unit->ModifyPowerRegenPercentBonus(power_type::Rage, 50.0f, true);

	CHECK(unit->GetEffectivePowerRegenPerTick(power_type::Rage, -3) == -3);
	CHECK(unit->GetEffectivePowerRegenPerTick(power_type::Rage, 0) == 0);
	CHECK(unit->GetEffectivePowerRegenPerTick(power_type::Rage, 10) == 15);
}

TEST_CASE("ModCritChanceTaken raises every attacker's crit chance against the unit", "[aura_scalars]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto attacker = MakeUnit(project, timers);
	auto victim = MakeUnit(project, timers);

	const float before = attacker->CriticalHitChance(*victim, weapon_attack::BaseAttack);
	REQUIRE(before < 100.0f);

	victim->ModifyCritChanceTakenBonus(100.0f, true);
	CHECK(attacker->CriticalHitChance(*victim, weapon_attack::BaseAttack) == Approx(100.0f));

	// The bonus belongs to the victim, not the attacker: attacking the *attacker* is unchanged.
	CHECK(victim->CriticalHitChance(*attacker, weapon_attack::BaseAttack) == Approx(before));

	victim->ModifyCritChanceTakenBonus(100.0f, false);
	CHECK(attacker->CriticalHitChance(*victim, weapon_attack::BaseAttack) == Approx(before));
}

TEST_CASE("The aura effect handlers drive the cached scalars on apply and misapply", "[aura_scalars]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeUnit(project, timers);

	const proto::SpellEntry spell = MakeSpell();
	AuraContainer container(*unit, /*casterId=*/0, spell, /*duration=*/0, /*itemGuid=*/0);

	proto::SpellEffect critEffect;
	critEffect.set_aura(static_cast<uint32>(aura_type::ModCritChanceTaken));
	AuraEffect critAura(container, critEffect, timers, /*basePoints=*/100);

	proto::SpellEffect manaEffect;
	manaEffect.set_aura(static_cast<uint32>(aura_type::ModPowerRegenPercent));
	manaEffect.set_miscvaluea(static_cast<int32>(power_type::Mana));
	AuraEffect manaAura(container, manaEffect, timers, /*basePoints=*/50);

	critAura.HandleEffect(true);
	manaAura.HandleEffect(true);

	CHECK(unit->GetCritChanceTakenBonus() == Approx(100.0f));
	CHECK(unit->GetEffectivePowerRegenPerTick(power_type::Mana, 20) == 30);

	critAura.HandleEffect(false);
	manaAura.HandleEffect(false);

	CHECK(unit->GetCritChanceTakenBonus() == Approx(0.0f));
	CHECK(unit->GetEffectivePowerRegenPerTick(power_type::Mana, 20) == 20);
}

TEST_CASE("An out-of-range power type on a regen aura is rejected, not written past the array",
	"[aura_scalars]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeUnit(project, timers);

	const proto::SpellEntry spell = MakeSpell();
	AuraContainer container(*unit, /*casterId=*/0, spell, /*duration=*/0, /*itemGuid=*/0);

	proto::SpellEffect effect;
	effect.set_aura(static_cast<uint32>(aura_type::ModPowerRegenPercent));
	effect.set_miscvaluea(static_cast<int32>(power_type::Count_));
	AuraEffect aura(container, effect, timers, /*basePoints=*/50);

	aura.HandleEffect(true);

	// Bad data must be logged and dropped; every power type stays untouched.
	for (uint8 i = 0; i < static_cast<uint8>(power_type::Count_); ++i)
	{
		CHECK(unit->GetEffectivePowerRegenPerTick(static_cast<PowerType>(i), 20) == 20);
	}
}
