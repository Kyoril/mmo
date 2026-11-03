// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// ModCastingSpeedPct raises (positive) or lowers (negative) the cast time of every spell its owner
// casts. Auras of different spells add their percentages (+30 and +20 make x1.50); the same spell's
// slow from several casters does not stack (the strongest counts). Procs stay instant.

#include "test_unit_factory.h"

#include "game_server/spells/aura_effect.h"
#include "game_server/spells/aura_container.h"
#include "game_server/spells/single_cast_state.h"
#include "game_server/spells/spell_cast.h"
#include "game/aura.h"
#include "game/spell_target_map.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

using namespace mmo;
using namespace mmo::test;

namespace
{
	struct SpeedFixture
	{
		asio::io_service io;
		TimerQueue timers{ io };
		proto::Project project;
		std::shared_ptr<GameUnitS> unit = MakeUnit(project, timers);
		proto::SpellEntry spell = MakeSpell();
		AuraContainer container{ *unit, 0, spell, 0, 0 };
		proto::SpellEffect effect;
		SpellTargetMap targetMap;

		SpeedFixture()
		{
			effect.set_aura(static_cast<uint32>(aura_type::ModCastingSpeedPct));
		}

		GameTime CastTime(const bool isProc = false)
		{
			SpellCast cast(timers, *unit);
			SingleCastState state(cast, spell, targetMap, 2000, isProc);
			return state.GetCastTime();
		}
	};
}

TEST_CASE_METHOD(SpeedFixture, "Without the aura the cast time is unchanged", "[casting_speed]")
{
	CHECK(unit->GetCastingSpeedMultiplier() == Approx(1.0f));
	CHECK(CastTime() == 2000);
}

TEST_CASE_METHOD(SpeedFixture, "ModCastingSpeedPct +30 slows a 2000 ms cast to 2600 ms", "[casting_speed]")
{
	AuraEffect aura(container, effect, timers, 30);
	aura.HandleEffect(true);
	CHECK(unit->GetCastingSpeedMultiplier() == Approx(1.3f));
	CHECK(CastTime() == 2600);
	CHECK(CastTime(true) == 0);

	aura.HandleEffect(false);
	CHECK(CastTime() == 2000);
}

TEST_CASE_METHOD(SpeedFixture, "Casting speed auras add their percentages", "[casting_speed]")
{
	proto::SpellEntry otherSpell = MakeSpell();
	otherSpell.set_id(2);
	AuraContainer otherContainer{ *unit, 0, otherSpell, 0, 0 };
	AuraEffect a(container, effect, timers, 30);
	AuraEffect b(otherContainer, effect, timers, 20);
	a.HandleEffect(true);
	b.HandleEffect(true);
	CHECK(unit->GetCastingSpeedMultiplier() == Approx(1.5f));
	CHECK(CastTime() == 3000);
}

TEST_CASE_METHOD(SpeedFixture, "Negative casting speed shortens casts and never goes below zero", "[casting_speed]")
{
	AuraEffect a(container, effect, timers, -20);
	a.HandleEffect(true);
	CHECK(CastTime() == 1600);

	AuraEffect b(container, effect, timers, -200);
	b.HandleEffect(true);
	CHECK(unit->GetCastingSpeedMultiplier() == Approx(0.0f));
	CHECK(CastTime() == 0);
}

TEST_CASE_METHOD(SpeedFixture, "The creature AI sees the modified cast time", "[casting_speed]")
{
	AuraEffect aura(container, effect, timers, 30);
	aura.HandleEffect(true);
	CHECK(unit->ModifyCastTime(spell.id(), 2000) == 2600);
}

// Tangled Tongue from several acolytes must not stack to +150% cast time: the same spell's slow
// counts once, from its strongest instance.
TEST_CASE_METHOD(SpeedFixture, "The same cast slow from two casters does not stack", "[casting_speed]")
{
	proto::SpellEntry curse = MakeSpell();
	curse.set_id(1360);
	AuraContainer firstCaster{ *unit, 0x4001, curse, 0, 0 };
	AuraContainer secondCaster{ *unit, 0x4002, curse, 0, 0 };
	AuraEffect first(firstCaster, effect, timers, 30);
	AuraEffect second(secondCaster, effect, timers, 30);
	first.HandleEffect(true);
	second.HandleEffect(true);
	CHECK(unit->GetCastingSpeedMultiplier() == Approx(1.3f));
	CHECK(CastTime() == 2600);

	first.HandleEffect(false);
	CHECK(unit->GetCastingSpeedMultiplier() == Approx(1.3f));
	second.HandleEffect(false);
	CHECK(unit->GetCastingSpeedMultiplier() == Approx(1.0f));
}

TEST_CASE_METHOD(SpeedFixture, "Casting speed haste of the same spell from two casters still adds", "[casting_speed]")
{
	AuraContainer firstCaster{ *unit, 0x4001, spell, 0, 0 };
	AuraContainer secondCaster{ *unit, 0x4002, spell, 0, 0 };
	AuraEffect first(firstCaster, effect, timers, -10);
	AuraEffect second(secondCaster, effect, timers, -10);
	first.HandleEffect(true);
	second.HandleEffect(true);
	CHECK(unit->GetCastingSpeedMultiplier() == Approx(0.8f));
}
