// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "test_unit_factory.h"

#include "game_server/spells/aura_container.h"
#include "game/spell.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

using namespace mmo;

// A spell's auras are applied after all of its effects ran. When a direct damage effect kills the
// target (Fireball's impact), OnKilled has already cleared the target's auras by the time the
// spell's damage-over-time aura arrives - it must not be applied to the corpse and tick there.

namespace
{
	proto::SpellEntry MakeAuraSpell(const uint32 id)
	{
		proto::SpellEntry spell = test::MakeSpell();
		spell.set_id(id);
		spell.set_baseid(id);
		spell.set_rank(1);
		return spell;
	}
}

TEST_CASE("An aura is not applied to a dead unit", "[dead_target_aura]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto target = test::MakeUnit(project, timers);
	target->Kill(nullptr);
	REQUIRE_FALSE(target->IsAlive());

	const proto::SpellEntry spell = MakeAuraSpell(500);
	target->ApplyAura(std::make_shared<AuraContainer>(*target, 99, spell, 5000, 0));

	CHECK_FALSE(target->HasAuraSpell(500));
}

TEST_CASE("An aura is applied to a living unit", "[dead_target_aura]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto target = test::MakeUnit(project, timers);
	REQUIRE(target->IsAlive());

	const proto::SpellEntry spell = MakeAuraSpell(500);
	target->ApplyAura(std::make_shared<AuraContainer>(*target, 99, spell, 5000, 0));

	CHECK(target->HasAuraSpell(500));
}

TEST_CASE("Passive and dead-target spells still apply their aura to a dead unit", "[dead_target_aura]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto target = test::MakeUnit(project, timers);
	target->Kill(nullptr);
	REQUIRE_FALSE(target->IsAlive());

	proto::SpellEntry passive = MakeAuraSpell(501);
	passive.set_attributes(0, spell_attributes::Passive);
	target->ApplyAura(std::make_shared<AuraContainer>(*target, target->GetGuid(), passive, 0, 0));
	CHECK(target->HasAuraSpell(501));

	proto::SpellEntry canTargetDead = MakeAuraSpell(502);
	canTargetDead.set_attributes(0, spell_attributes::CanTargetDead);
	target->ApplyAura(std::make_shared<AuraContainer>(*target, 99, canTargetDead, 5000, 0));
	CHECK(target->HasAuraSpell(502));

	proto::SpellEntry castableWhileDead = MakeAuraSpell(503);
	castableWhileDead.set_attributes(0, spell_attributes::CastableWhileDead);
	target->ApplyAura(std::make_shared<AuraContainer>(*target, target->GetGuid(), castableWhileDead, 5000, 0));
	CHECK(target->HasAuraSpell(503));
}
