// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#include "catch.hpp"
#include "game_server/player_duel.h"
#include "game_server/objects/game_player_s.h"
#include "game_server/objects/game_creature_s.h"
#include "game/object_type_id.h"
#include "base/timer_queue.h"
#include "proto_data/project.h"
#include "test_unit_factory.h"
#include "asio/io_service.hpp"
#include <tuple>

using namespace mmo;
namespace
{
	struct DuelFixture
	{
		asio::io_service io;
		TimerQueue timers{io};
		proto::Project project;
		std::shared_ptr<GamePlayerS> a, b;
		std::vector<std::tuple<uint64, DuelEvent, uint8>> events;
		std::shared_ptr<PlayerDuel> duel;
		DuelFixture()
		{
			project.factionTemplates.add(0)->set_faction(9);
			auto* cls = project.classes.add(1);
			cls->set_powertype(proto::ClassEntry_PowerType_MANA);
			for (int i = 0; i < 2; ++i)
			{
				auto* level = cls->add_levelbasevalues();
				level->set_health(100); level->set_mana(100); level->set_stamina(10);
			}
			a = std::make_shared<GamePlayerS>(project, timers);
			b = std::make_shared<GamePlayerS>(project, timers);
			for (auto unit : {a, b})
			{
				unit->Initialize(); unit->SetClass(*cls); unit->SetLevel(1);
				unit->Set<uint32>(object_fields::MaxHealth, 1000);
				unit->Set<uint32>(object_fields::Health, 1000);
			}
			a->Set<uint64>(object_fields::Guid, 1); b->Set<uint64>(object_fields::Guid, 2);
			duel = std::make_shared<PlayerDuel>(a, b,
				[this](uint64 who, DuelEvent event, uint8 detail, const String&, const Vector3&)
				{ events.emplace_back(who, event, detail); }, 1000);
			duel->Start();
		}
		void Begin() { duel->Accept(2, 1000); duel->Update(4000); }
	};
}

TEST_CASE("Duel acceptance counts 3 2 1 before pairwise hostility", "[duel]")
{
	DuelFixture f;
	REQUIRE(f.events.size() == 2);
	CHECK(f.a->UnitIsFriendly(*f.b));
	f.duel->Accept(1, 1000); // The sender cannot accept their own request.
	CHECK(f.events.size() == 2);
	f.duel->Accept(2, 1000);
	CHECK(std::get<2>(f.events.back()) == 3);
	f.duel->Update(2000); CHECK(std::get<2>(f.events.back()) == 2);
	f.duel->Update(3000); CHECK(std::get<2>(f.events.back()) == 1);
	CHECK_FALSE(f.a->IsDuelActive());
	f.duel->Update(4000);
	CHECK(f.a->UnitIsEnemy(*f.b)); CHECK_FALSE(f.a->UnitIsFriendly(*f.b));
	CHECK(f.b->IsDuelActive());
}

TEST_CASE("A duel cannot be accepted after the invitee changes faction", "[duel][faction]")
{
	DuelFixture f;
	f.project.factionTemplates.add(7)->set_faction(7);
	f.b->Set<uint32>(object_fields::FactionTemplate, 7);
	f.duel->Accept(2, 1000);
	CHECK(f.duel->IsFinished());
	CHECK_FALSE(f.a->IsDuelActive());
	CHECK(f.a->GetDuelPartner() == 0);
	CHECK(f.b->GetDuelPartner() == 0);
}

TEST_CASE("Duel damage cannot land during invitation or countdown", "[duel]")
{
	DuelFixture f;
	CHECK(f.b->Damage(2000, spell_school::Normal, f.a.get(), damage_type::AttackSwing) == 0);
	f.duel->Accept(2, 1000);
	CHECK(f.b->Damage(2000, spell_school::Normal, f.a.get(), damage_type::AttackSwing) == 0);
	CHECK(f.b->GetHealth() == 1000);
}

TEST_CASE("Duel overkill stops at one percent and resolves once without death", "[duel]")
{
	DuelFixture f; f.Begin();
	CHECK(f.b->Damage(2000, spell_school::Normal, f.a.get(), damage_type::AttackSwing) == 990);
	CHECK(f.b->GetHealth() == 10); CHECK(f.b->IsAlive()); CHECK(f.duel->IsFinished());
	CHECK(f.b->GetStandState() == unit_stand_state::Kneel);
	CHECK(f.a->GetStandState() == unit_stand_state::Stand);
	CHECK(f.a->GetDuelPartner() == 0); CHECK(f.b->GetDuelPartner() == 0);
	REQUIRE(f.events.size() >= 2);
	CHECK(std::get<2>(f.events[f.events.size() - 2]) == static_cast<uint8>(DuelResult::Won));
	CHECK(std::get<2>(f.events.back()) == static_cast<uint8>(DuelResult::Lost));
	const auto count = f.events.size();
	CHECK(f.b->Damage(2000, spell_school::Normal, f.a.get(), damage_type::AttackSwing) == 0);
	f.duel->Cancel(2); f.duel->Update(99999);
	CHECK(f.events.size() == count); CHECK(f.b->GetHealth() == 10);
}

TEST_CASE("Duel loser stands after five seconds without overriding another pose", "[duel]")
{
	DuelFixture f; f.Begin();
	f.b->Damage(2000, spell_school::Normal, f.a.get(), damage_type::AttackSwing);
	REQUIRE(f.b->GetStandState() == unit_stand_state::Kneel);
	// The pose timer must outlive the completed duel session.
	f.duel.reset();
	f.io.run_for(std::chrono::milliseconds(4000));
	CHECK(f.b->GetStandState() == unit_stand_state::Kneel);
	SECTION("automatically return to idle")
	{
		f.io.run_for(std::chrono::milliseconds(1200));
		CHECK(f.b->GetStandState() == unit_stand_state::Stand);
	}
	SECTION("preserve a different pose chosen before the timer expires")
	{
		f.b->SetStandState(unit_stand_state::Sit);
		f.io.run_for(std::chrono::milliseconds(1200));
		CHECK(f.b->GetStandState() == unit_stand_state::Sit);
	}
}

TEST_CASE("Duel health floor rounds safely for small and non-divisible pools", "[duel]")
{
	CHECK(DuelHealthFloor(0) == 1); CHECK(DuelHealthFloor(1) == 1);
	CHECK(DuelHealthFloor(99) == 1); CHECK(DuelHealthFloor(101) == 2);
	DuelFixture f; f.b->Set<uint32>(object_fields::MaxHealth, 101);
	f.b->Set<uint32>(object_fields::Health, 101); f.Begin();
	f.b->Damage(99, spell_school::Normal, f.a.get(), damage_type::AttackSwing);
	CHECK(f.b->GetHealth() == 2); CHECK(f.duel->IsFinished());
}

TEST_CASE("Periodic duel damage ends at the same floor and cannot keep ticking afterward", "[duel]")
{
	DuelFixture f; f.Begin();
	f.b->Set<uint32>(object_fields::Health, 20);
	CHECK(f.b->Damage(15, spell_school::Fire, f.a.get(), damage_type::Periodic) == 10);
	REQUIRE(f.duel->IsFinished());
	CHECK(f.b->IsAlive()); CHECK(f.b->GetHealth() == 10);
	CHECK(f.b->Damage(15, spell_school::Fire, f.a.get(), damage_type::Periodic) == 0);
	CHECK(f.b->GetHealth() == 10);
}

TEST_CASE("A duel does not protect against outside or environmental damage", "[duel][damage_source]")
{
	DuelFixture f;
	f.Begin();
	std::shared_ptr<GameUnitS> outsider = test::MakeUnit(f.project, f.timers);
	outsider->Set<uint64>(object_fields::Guid, 3);
	GameUnitS* attacker = outsider.get();
	SECTION("third player") {}
	SECTION("creature")
	{
		auto* entry = f.project.units.add(3);
		entry->set_name("Outside attacker");
		entry->set_minlevel(1);
		entry->set_maxlevel(1);
		outsider = std::make_shared<GameCreatureS>(f.project, f.timers, *entry);
		outsider->Initialize();
		outsider->Set<uint64>(object_fields::Guid, 3);
		attacker = outsider.get();
	}
	SECTION("environment") { attacker = nullptr; }
	SECTION("another player's summon") { outsider->Set<uint64>(object_fields::Owner, 4); }

	bool died = false;
	scoped_connection death{ f.b->killed.connect([&](GameUnitS*) { died = true; }) };
	CHECK(f.b->Damage(995, spell_school::Fire, attacker, damage_type::Periodic) == 995);
	CHECK(f.b->GetHealth() == 5);
	CHECK_FALSE(f.duel->IsFinished());
	CHECK(f.b->Damage(2000, spell_school::Normal, attacker, damage_type::AttackSwing) == 5);
	CHECK_FALSE(f.b->IsAlive());
	CHECK(died);
	f.duel->Update(5000);
	CHECK(f.duel->IsFinished());
	CHECK(std::get<2>(f.events.back()) == static_cast<uint8>(DuelResult::Cancelled));
}

TEST_CASE("Owned duel damage respects invitation, defeat and post-duel protection", "[duel][damage_source]")
{
	DuelFixture f;
	auto summon = test::MakeUnit(f.project, f.timers);
	summon->Set<uint64>(object_fields::Guid, 3);
	summon->Set<uint64>(object_fields::Owner, f.a->GetGuid());
	CHECK(f.b->Damage(2000, spell_school::Fire, summon.get(), damage_type::Periodic) == 0);
	f.Begin();
	CHECK(f.b->Damage(2000, spell_school::Fire, summon.get(), damage_type::Periodic) == 990);
	CHECK(f.b->GetHealth() == 10);
	CHECK(f.duel->IsFinished());
	CHECK(std::get<2>(f.events.back()) == static_cast<uint8>(DuelResult::Lost));
	const auto eventCount = f.events.size();
	CHECK(f.b->Damage(2000, spell_school::Fire, summon.get(), damage_type::Periodic) == 0);
	CHECK(f.b->IsAlive());
	CHECK(f.events.size() == eventCount);
}

TEST_CASE("Declined and expired duels clear both participants without hostility", "[duel]")
{
	DuelFixture f;
	SECTION("decline") { f.duel->Cancel(2, DuelResult::Declined); }
	SECTION("expiry") { f.duel->Update(61000); }
	REQUIRE(f.duel->IsFinished());
	CHECK_FALSE(f.a->IsDuelActive()); CHECK(f.a->GetDuelPartner() == 0); CHECK(f.b->GetDuelPartner() == 0);
	CHECK(f.a->GetStandState() == unit_stand_state::Stand);
	CHECK(f.b->GetStandState() == unit_stand_state::Stand);
	CHECK(std::get<1>(f.events.back()) == DuelEvent::Finished);
}

TEST_CASE("An active duel can be forfeited and a dead invitee cannot accept", "[duel]")
{
	DuelFixture f;
	SECTION("forfeit") { f.Begin(); f.duel->Cancel(2); CHECK(std::get<2>(f.events.back()) == static_cast<uint8>(DuelResult::Lost)); }
	SECTION("dead invitee") { f.b->Set<uint32>(object_fields::Health, 0); f.duel->Accept(2, 1000); }
	CHECK(f.duel->IsFinished()); CHECK_FALSE(f.a->IsDuelActive());
}

TEST_CASE("Duel completion removes harmful opponent auras and rejects late application", "[duel]")
{
	DuelFixture f;
	// Spell entries must outlive the fixture's units, which hold references to them.
	static proto::SpellEntry harmful = []()
	{
		auto spell = test::MakeSpell(); spell.set_id(9901); spell.set_baseid(9901);
		auto* effect = spell.add_effects(); effect->set_type(spell_effects::ApplyAura);
		effect->set_aura(aura_type::ModDamageTakenPct); effect->set_targeta(spell_effect_targets::TargetEnemy);
		return spell;
	}();
	static proto::SpellEntry positive = []()
	{
		auto spell = test::MakeSpell(); spell.set_id(9902); spell.set_baseid(9902);
		auto* effect = spell.add_effects(); effect->set_type(spell_effects::ApplyAura);
		effect->set_aura(aura_type::ModDamageTakenPct); effect->set_targeta(spell_effect_targets::Caster);
		return spell;
	}();
	auto apply = [&](const proto::SpellEntry& spell)
	{
		auto aura = std::make_shared<AuraContainer>(*f.b, f.a->GetGuid(), spell, 60000, 0);
		aura->AddAuraEffect(spell.effects(0), 0);
		f.b->ApplyAura(std::move(aura));
	};
	f.Begin(); apply(harmful); apply(positive);
	REQUIRE(f.b->HasAuraSpell(9901)); REQUIRE(f.b->HasAuraSpell(9902));
	f.b->Damage(5000, spell_school::Normal, f.a.get(), damage_type::AttackSwing);
	CHECK_FALSE(f.b->HasAuraSpell(9901)); CHECK(f.b->HasAuraSpell(9902));
	apply(harmful); CHECK_FALSE(f.b->HasAuraSpell(9901)); CHECK(f.b->IsAlive());
}
