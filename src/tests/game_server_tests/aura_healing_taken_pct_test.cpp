// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// ModHealingTakenPct scales every spell heal its owner receives (direct, periodic, percentage and
// leech heals). Auras of different spells add their percentages; reductions of the same spell from
// several casters do not stack (the strongest counts). The multiplier never drops below zero.

#include "test_unit_factory.h"

#include "game_server/spells/aura_effect.h"
#include "game_server/spells/aura_container.h"
#include "game_server/spells/spell_effects.h"
#include "game_server/spells/spell_cast_context.h"
#include "game_server/spells/spell_cast.h"
#include "game/aura.h"
#include "game/spell_target_map.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

#include <chrono>

using namespace mmo;
using namespace mmo::test;

namespace
{
	struct HealFixture
	{
		asio::io_service io;
		TimerQueue timers{ io };
		proto::Project project;
		std::shared_ptr<GamePlayerS> healer = MakeUnit(project, timers);
		std::shared_ptr<GamePlayerS> unit = MakeUnit(project, timers);
		proto::SpellEntry spell = MakeSpell();
		AuraContainer container{ *unit, 0, spell, 0, 0 };
		proto::SpellEffect auraEffect;
		proto::SpellEffect healEffect;
		SpellTargetMap targetMap;
		std::unique_ptr<SpellCast> cast;
		std::unique_ptr<SpellCastContext> castCtx;
		std::vector<GameObjectS*> targets;

		HealFixture()
		{
			auraEffect.set_aura(static_cast<uint32>(aura_type::ModHealingTakenPct));

			healer->Set<uint64>(object_fields::Guid, 0x3001);
			unit->Set<uint64>(object_fields::Guid, 0x3002);
			unit->Set<uint32>(object_fields::MaxHealth, 1000);
			unit->Set<uint32>(object_fields::Health, 500);
			healer->Set<uint32>(object_fields::MaxHealth, 1000);
			healer->Set<uint32>(object_fields::Health, 1000);

			healEffect.set_powerbonusfactor(0.0f);
			cast = std::make_unique<SpellCast>(timers, *healer);
			castCtx = std::make_unique<SpellCastContext>(*cast, spell, targetMap);
			targets.push_back(unit.get());
		}

		SpellEffectContext MakeCtx(const int32 basePoints)
		{
			return SpellEffectContext{
				*castCtx, healEffect, basePoints, targets,
				[](GameUnitS&) -> AuraContainer&
				{
					FAIL("no auras expected");
					std::abort();
				},
				[](GameObjectS&) {}
			};
		}
	};
}

TEST_CASE_METHOD(HealFixture, "Without the aura heals are unchanged", "[healing_taken_pct]")
{
	CHECK(unit->GetHealingTakenMultiplier() == Approx(1.0f));
	CHECK(unit->Heal(100, healer.get()) == 100);
	CHECK(unit->GetHealth() == 600u);
}

TEST_CASE_METHOD(HealFixture, "ModHealingTakenPct -20 turns a direct heal of 100 into 80 and removal restores it", "[healing_taken_pct]")
{
	AuraEffect aura(container, auraEffect, timers, -20);
	aura.HandleEffect(true);
	CHECK(unit->GetHealingTakenMultiplier() == Approx(0.8f));

	auto ctx = MakeCtx(100);
	SpellEffects::HandleHeal(ctx);
	// HandleHeal can roll a spell crit (doubles the heal before the aura scales it), so accept both.
	const uint32 reduced = unit->GetHealth() - 500u;
	CHECK((reduced == 80u || reduced == 160u));

	aura.HandleEffect(false);
	CHECK(unit->GetHealingTakenMultiplier() == Approx(1.0f));
	const uint32 before = unit->GetHealth();
	auto ctx2 = MakeCtx(100);
	SpellEffects::HandleHeal(ctx2);
	const uint32 restored = unit->GetHealth() - before;
	CHECK((restored == 100u || restored == 200u));
}

TEST_CASE_METHOD(HealFixture, "ModHealingTakenPct scales percentage heals", "[healing_taken_pct]")
{
	AuraEffect aura(container, auraEffect, timers, -20);
	aura.HandleEffect(true);

	auto ctx = MakeCtx(10);	// 10% of 1000 = 100 -> 80
	SpellEffects::HandleHealPct(ctx);
	CHECK(unit->GetHealth() == 580u);
}

TEST_CASE_METHOD(HealFixture, "ModHealingTakenPct scales the self-heal of a leech", "[healing_taken_pct]")
{
	// The unit under the aura is the one healing itself through HealthLeech.
	auto* leechSpell = project.spells.add(911);
	leechSpell->add_attributes(0);
	leechSpell->add_attributes(0);
	healEffect.set_type(spell_effects::HealthLeech);
	healEffect.set_multiplevalue(1.0f);

	healer->Set<uint32>(object_fields::MaxHealth, 100);
	healer->Set<uint32>(object_fields::Health, 50);
	AuraContainer healerContainer(*healer, 0, spell, 0, 0);
	AuraEffect aura(healerContainer, auraEffect, timers, -50);
	aura.HandleEffect(true);

	unit->Set<uint32>(object_fields::MaxHealth, 1000);
	unit->Set<uint32>(object_fields::Health, 1000);
	SpellCastContext leechCtx(*cast, *leechSpell, targetMap);
	SpellEffectContext ctx{
		leechCtx, healEffect, 10, targets,
		[](GameUnitS&) -> AuraContainer&
		{
			FAIL("no auras expected");
			std::abort();
		},
		[](GameObjectS&) {}
	};
	SpellEffects::HandleHealthLeech(ctx);

	const uint32 dealt = 1000u - unit->GetHealth();
	REQUIRE(dealt > 1u);
	CHECK(healer->GetHealth() == 50u + dealt / 2u);
}

TEST_CASE_METHOD(HealFixture, "Healing taken auras add their percentages and never go below zero", "[healing_taken_pct]")
{
	proto::SpellEntry otherSpell = MakeSpell();
	otherSpell.set_id(2);
	proto::SpellEntry thirdSpell = MakeSpell();
	thirdSpell.set_id(3);
	AuraContainer otherContainer{ *unit, 0, otherSpell, 0, 0 };
	AuraContainer thirdContainer{ *unit, 0, thirdSpell, 0, 0 };

	AuraEffect a(container, auraEffect, timers, -20);
	AuraEffect b(otherContainer, auraEffect, timers, -10);
	a.HandleEffect(true);
	b.HandleEffect(true);
	CHECK(unit->GetHealingTakenMultiplier() == Approx(0.7f));
	CHECK(unit->Heal(100, healer.get()) == 70);

	AuraEffect c(thirdContainer, auraEffect, timers, -200);
	c.HandleEffect(true);
	CHECK(unit->GetHealingTakenMultiplier() == Approx(0.0f));
	CHECK(unit->Heal(100, healer.get()) == 0);
}

TEST_CASE_METHOD(HealFixture, "ModHealingTakenPct scales each periodic heal tick", "[healing_taken_pct]")
{
	AuraEffect reduce(container, auraEffect, timers, -20);
	reduce.HandleEffect(true);

	// One tick after ~10 ms: 100 healed -> 80.
	auto hotContainer = std::make_shared<AuraContainer>(*unit, 0, spell, 10, 0);
	proto::SpellEffect hotEffect;
	hotEffect.set_aura(static_cast<uint32>(aura_type::PeriodicHeal));
	hotEffect.set_amplitude(10);
	auto hot = std::make_shared<AuraEffect>(*hotContainer, hotEffect, timers, 100);
	REQUIRE(hot->GetMaxTickCount() == 1u);	// duration / amplitude: a slow machine cannot fire a second tick
	hot->HandleEffect(true);

	io.run_for(std::chrono::milliseconds(200));
	CHECK(unit->GetHealth() == 580u);
}

// Combat text and heal procs report the healed amount after the healing taken percentage, rounded
// exactly as Heal() applies it: under -20% a heal of 100 reads 80, not 100.
TEST_CASE_METHOD(HealFixture, "The scaled heal amount matches what Heal applies", "[healing_taken_pct]")
{
	AuraEffect aura(container, auraEffect, timers, -20);
	aura.HandleEffect(true);

	CHECK(unit->ScaleHealingTaken(100) == 80u);
	CHECK(unit->ScaleHealingTaken(333) == 266u);	// 266.4 rounds down, like Heal()
	CHECK(unit->Heal(333, healer.get()) == static_cast<int32>(unit->ScaleHealingTaken(333)));

	aura.HandleEffect(false);
	CHECK(unit->ScaleHealingTaken(333) == 333u);
}

// Lingering Dread from five acolytes must not cut healing to nothing: the same spell's reduction
// counts once, from its strongest instance, while a different spell still adds.
TEST_CASE_METHOD(HealFixture, "The same healing reduction from two casters does not stack", "[healing_taken_pct]")
{
	spell.set_id(1365);
	AuraContainer firstCaster{ *unit, 0x4001, spell, 0, 0 };
	AuraContainer secondCaster{ *unit, 0x4002, spell, 0, 0 };
	AuraEffect first(firstCaster, auraEffect, timers, -20);
	AuraEffect second(secondCaster, auraEffect, timers, -20);
	first.HandleEffect(true);
	second.HandleEffect(true);
	CHECK(unit->GetHealingTakenMultiplier() == Approx(0.8f));
	CHECK(unit->Heal(100, healer.get()) == 80);

	// A weaker instance of the same spell changes nothing; the strongest one counts.
	AuraContainer thirdCaster{ *unit, 0x4003, spell, 0, 0 };
	AuraEffect weaker(thirdCaster, auraEffect, timers, -10);
	weaker.HandleEffect(true);
	CHECK(unit->GetHealingTakenMultiplier() == Approx(0.8f));

	// Removing one of the strongest keeps the other; removing both falls back to the weaker one.
	first.HandleEffect(false);
	CHECK(unit->GetHealingTakenMultiplier() == Approx(0.8f));
	second.HandleEffect(false);
	CHECK(unit->GetHealingTakenMultiplier() == Approx(0.9f));
	weaker.HandleEffect(false);
	CHECK(unit->GetHealingTakenMultiplier() == Approx(1.0f));
}

TEST_CASE_METHOD(HealFixture, "Healing bonuses of the same spell from two casters still add", "[healing_taken_pct]")
{
	AuraContainer firstCaster{ *unit, 0x4001, spell, 0, 0 };
	AuraContainer secondCaster{ *unit, 0x4002, spell, 0, 0 };
	AuraEffect first(firstCaster, auraEffect, timers, 10);
	AuraEffect second(secondCaster, auraEffect, timers, 10);
	first.HandleEffect(true);
	second.HandleEffect(true);
	CHECK(unit->GetHealingTakenMultiplier() == Approx(1.2f));
}
