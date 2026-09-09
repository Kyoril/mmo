// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "test_unit_factory.h"

#include "game_server/spells/aura_effect.h"
#include "game_server/spells/aura_container.h"
#include "game_server/objects/game_player_s.h"
#include "game/aura.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "shared/proto_data/spells.pb.h"
#include "shared/proto_data/classes.pb.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

#include <memory>

using namespace mmo;
using namespace mmo::test;

// ---------------------------------------------------------------------------
// Construction / accessor tests
// ---------------------------------------------------------------------------

TEST_CASE("AuraEffect GetType returns the aura type set in the SpellEffect proto", "[aura_effect]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto unit = MakeUnit(project, timers);

	proto::SpellEntry spell;
	AuraContainer container(*unit, /*casterId=*/0, spell, /*duration=*/0, /*itemGuid=*/0);

	proto::SpellEffect effect;
	effect.set_aura(static_cast<uint32>(aura_type::PeriodicDamage));

	AuraEffect aura(container, effect, timers, /*basePoints=*/10);

	CHECK(aura.GetType() == AuraType::PeriodicDamage);
}

TEST_CASE("AuraEffect GetBasePoints returns the value passed at construction", "[aura_effect]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto unit = MakeUnit(project, timers);

	proto::SpellEntry spell;
	AuraContainer container(*unit, /*casterId=*/0, spell, /*duration=*/0, /*itemGuid=*/0);

	proto::SpellEffect effect;
	effect.set_aura(static_cast<uint32>(aura_type::None));

	AuraEffect aura(container, effect, timers, /*basePoints=*/42);

	CHECK(aura.GetBasePoints() == 42);
}

TEST_CASE("AuraEffect IsPeriodic is false before HandleEffect is called", "[aura_effect]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto unit = MakeUnit(project, timers);

	proto::SpellEntry spell;
	AuraContainer container(*unit, /*casterId=*/0, spell, /*duration=*/0, /*itemGuid=*/0);

	proto::SpellEffect effect;
	effect.set_aura(static_cast<uint32>(aura_type::PeriodicDamage));

	AuraEffect aura(container, effect, timers, /*basePoints=*/5);

	CHECK_FALSE(aura.IsPeriodic());
}

TEST_CASE("AuraEffect GetMaxTickCount is zero when effect amplitude is zero", "[aura_effect]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto unit = MakeUnit(project, timers);

	proto::SpellEntry spell;
	// duration > 0 but amplitude = 0 → totalTicks = 0 (no division)
	AuraContainer container(*unit, /*casterId=*/0, spell, /*duration=*/5000, /*itemGuid=*/0);

	proto::SpellEffect effect;
	effect.set_aura(static_cast<uint32>(aura_type::PeriodicHeal));
	effect.set_amplitude(0);  // no periodic tick interval set

	AuraEffect aura(container, effect, timers, /*basePoints=*/20);

	CHECK(aura.GetMaxTickCount() == 0u);
}

TEST_CASE("AuraEffect GetMaxTickCount equals duration divided by amplitude when both are positive", "[aura_effect]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto unit = MakeUnit(project, timers);

	proto::SpellEntry spell;
	// duration = 6000 ms, amplitude = 2000 ms → 3 ticks
	AuraContainer container(*unit, /*casterId=*/0, spell, /*duration=*/6000, /*itemGuid=*/0);

	proto::SpellEffect effect;
	effect.set_aura(static_cast<uint32>(aura_type::PeriodicDamage));
	effect.set_amplitude(2000);

	AuraEffect aura(container, effect, timers, /*basePoints=*/15);

	CHECK(aura.GetMaxTickCount() == 3u);
}

// ---------------------------------------------------------------------------
// IsPeriodic — HandleEffect(apply=true) starts periodic for the periodic types
// ---------------------------------------------------------------------------

TEST_CASE("AuraEffect IsPeriodic is true after HandleEffect(apply=true) for PeriodicDamage", "[aura_effect]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto unit = MakeUnit(project, timers);
	// Set health so Damage() calls don't short-circuit
	unit->Set<uint32>(object_fields::Health, 1000);
	unit->Set<uint32>(object_fields::MaxHealth, 1000);

	auto spell = MakeSpell();
	// amplitude > 0 so HandlePeriodicBase sets up ticks
	AuraContainer container(*unit, /*casterId=*/0, spell, /*duration=*/3000, /*itemGuid=*/0);

	proto::SpellEffect effect;
	effect.set_aura(static_cast<uint32>(aura_type::PeriodicDamage));
	effect.set_amplitude(1000);

	AuraEffect aura(container, effect, timers, /*basePoints=*/5);
	CHECK_FALSE(aura.IsPeriodic());

	aura.HandleEffect(/*apply=*/true);

	CHECK(aura.IsPeriodic());
}

TEST_CASE("AuraEffect IsPeriodic is true after HandleEffect(apply=true) for PeriodicHeal", "[aura_effect]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto unit = MakeUnit(project, timers);
	unit->Set<uint32>(object_fields::Health, 500);
	unit->Set<uint32>(object_fields::MaxHealth, 1000);

	auto spell = MakeSpell();
	AuraContainer container(*unit, /*casterId=*/0, spell, /*duration=*/4000, /*itemGuid=*/0);

	proto::SpellEffect effect;
	effect.set_aura(static_cast<uint32>(aura_type::PeriodicHeal));
	effect.set_amplitude(1000);

	AuraEffect aura(container, effect, timers, /*basePoints=*/20);

	aura.HandleEffect(/*apply=*/true);

	CHECK(aura.IsPeriodic());
}

TEST_CASE("AuraEffect IsPeriodic is true after HandleEffect(apply=true) for PeriodicEnergize", "[aura_effect]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto unit = MakeUnit(project, timers);

	auto spell = MakeSpell();
	AuraContainer container(*unit, /*casterId=*/0, spell, /*duration=*/3000, /*itemGuid=*/0);

	proto::SpellEffect effect;
	effect.set_aura(static_cast<uint32>(aura_type::PeriodicEnergize));
	effect.set_amplitude(1000);
	effect.set_miscvaluea(0); // power type: Mana

	AuraEffect aura(container, effect, timers, /*basePoints=*/10);

	aura.HandleEffect(/*apply=*/true);

	CHECK(aura.IsPeriodic());
}

TEST_CASE("AuraEffect IsPeriodic stays false for non-periodic aura types", "[aura_effect]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto unit = MakeUnit(project, timers);

	auto spell = MakeSpell();
	AuraContainer container(*unit, /*casterId=*/0, spell, /*duration=*/3000, /*itemGuid=*/0);

	proto::SpellEffect effect;
	effect.set_aura(static_cast<uint32>(aura_type::ModStat));
	effect.set_amplitude(1000);
	effect.set_miscvaluea(1); // stat index 1 = Strength

	AuraEffect aura(container, effect, timers, /*basePoints=*/10);
	aura.HandleEffect(/*apply=*/true);
	CHECK_FALSE(aura.IsPeriodic());
	aura.HandleEffect(/*apply=*/false);
}

// ---------------------------------------------------------------------------
// Tick counter — GetTickCount increments when ticks fire via io_service::run
// ---------------------------------------------------------------------------

TEST_CASE("AuraEffect tick count starts at zero", "[aura_effect]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto unit = MakeUnit(project, timers);
	unit->Set<uint32>(object_fields::Health, 1000);
	unit->Set<uint32>(object_fields::MaxHealth, 1000);

	proto::SpellEntry spell;
	AuraContainer container(*unit, /*casterId=*/0, spell, /*duration=*/3000, /*itemGuid=*/0);

	proto::SpellEffect effect;
	effect.set_aura(static_cast<uint32>(aura_type::PeriodicDamage));
	effect.set_amplitude(1000);

	auto aura = std::make_shared<AuraEffect>(container, effect, timers, /*basePoints=*/5);

	CHECK(aura->GetTickCount() == 0u);
}

TEST_CASE("AuraEffect GetTickInterval equals effect amplitude", "[aura_effect]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto unit = MakeUnit(project, timers);

	proto::SpellEntry spell;
	AuraContainer container(*unit, /*casterId=*/0, spell, /*duration=*/6000, /*itemGuid=*/0);

	proto::SpellEffect effect;
	effect.set_aura(static_cast<uint32>(aura_type::PeriodicHeal));
	effect.set_amplitude(2000);

	AuraEffect aura(container, effect, timers, /*basePoints=*/10);

	CHECK(aura.GetTickInterval() == 2000);
}
