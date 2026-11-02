// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// ModHealingTakenPct scales every spell heal its owner receives (direct, periodic, percentage and
// leech heals). Several auras add their percentages; the multiplier never drops below zero.

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

		uint32 Gained(const uint32 before) const { return unit->GetHealth() - before; }


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
	CHECK(unit->GetHealth() == 580u);

	aura.HandleEffect(false);
	CHECK(unit->GetHealingTakenMultiplier() == Approx(1.0f));
	auto ctx2 = MakeCtx(100);
	SpellEffects::HandleHeal(ctx2);
	CHECK(unit->GetHealth() == 680u);
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
	AuraEffect a(container, auraEffect, timers, -20);
	AuraEffect b(container, auraEffect, timers, -10);
	a.HandleEffect(true);
	b.HandleEffect(true);
	CHECK(unit->GetHealingTakenMultiplier() == Approx(0.7f));
	CHECK(unit->Heal(100, healer.get()) == 70);

	AuraEffect c(container, auraEffect, timers, -200);
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
	hot->HandleEffect(true);

	io.run_for(std::chrono::milliseconds(200));
	CHECK(unit->GetHealth() == 580u);
}
