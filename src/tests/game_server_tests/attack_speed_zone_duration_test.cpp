// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// ModAttackSpeed changes the melee swing interval of players, creatures and pets: +30 swings 30%
// faster (interval / 1.30), -15 slower (interval / 0.85), and auras add their percentages. The
// BaseAttackTime field the client displays follows the effective main-hand interval. A swing
// already scheduled keeps its time; the next one uses the new interval.
//
// PersistentAreaAura ground zones live for the spell duration after the caster's Duration spell
// mods, like summons do.

#include "catch.hpp"
#include "persistent_pet_fixture.h"
#include "test_unit_factory.h"

#include "game_server/spells/aura_container.h"
#include "game_server/spells/aura_effect.h"
#include "game_server/world/spell_zone_set.h"
#include "game/aura.h"
#include "base/clock.h"

#include <cmath>

using namespace mmo;
using namespace mmo::pet_tests;

namespace
{
	constexpr uint32 SlowAttackSpellId = 950;
	constexpr uint32 PetFrenzySpellId = 951;
	constexpr uint32 SelfHasteSpellId = 952;
	constexpr uint32 ZoneSpellId = 960;
	constexpr uint32 ZoneDurationTalentId = 961;

	constexpr uint64 ZoneFamily = 0x8;

	/// A minimal concrete unit whose swing timer can be armed without a world.
	class SwingUnit final : public GameUnitS
	{
	public:
		SwingUnit(const proto::Project& project, TimerQueue& timers)
			: GameUnitS(project, timers)
		{
		}

		const String& GetName() const override { return m_name; }

		void RaiseCastEnded(const bool succeeded) { OnSpellCastEnded(succeeded); }

		using GameUnitS::StopSwingForCast;

		void BeginAttacking(const std::shared_ptr<GameUnitS>& victim)
		{
			SetVictim(victim);
			TriggerNextAutoAttack();
		}

	private:
		String m_name = "SwingUnit";
	};

	struct AttackSpeedFixture : PersistentPetFixture
	{
		AttackSpeedFixture()
		{
			AddAuraSpell(SlowAttackSpellId, 0, aura_type::ModAttackSpeed, -15, spell_effect_targets::TargetEnemy);
			AddAuraSpell(PetFrenzySpellId, 0, aura_type::ModAttackSpeed, 30, spell_effect_targets::Pet);
			AddAuraSpell(SelfHasteSpellId, 0, aura_type::ModAttackSpeed, 30, spell_effect_targets::Caster);

			// A ground zone of 8 seconds, ticking every second
			auto* zone = AddSpell(ZoneSpellId, ZoneFamily, 0);
			zone->set_duration(8000);
			auto* zoneEffect = zone->add_effects();
			zoneEffect->set_index(0);
			zoneEffect->set_type(spell_effects::PersistentAreaAura);
			zoneEffect->set_targeta(spell_effect_targets::Caster);
			zoneEffect->set_radius(5.0f);
			zoneEffect->set_amplitude(1000);

			// +4 seconds on the zone's family
			auto* talent = AddAuraSpell(ZoneDurationTalentId, spell_attributes::Passive, aura_type::AddFlatModifier, 4000, spell_effect_targets::Caster);
			talent->mutable_effects(0)->set_miscvaluea(spell_mod_op::Duration);
			talent->mutable_effects(0)->set_affectmask(ZoneFamily);
		}

		proto::SpellEntry* AddSpell(const uint32 id, const uint64 familyFlags, const uint32 attributes)
		{
			auto* spell = project.spells.add(id);
			spell->set_name("Test Spell");
			spell->add_attributes(attributes);
			spell->add_attributes(0);
			spell->set_familyflags(familyFlags);
			spell->set_rank(1);
			spell->set_baseid(id);
			return spell;
		}

		proto::SpellEntry* AddAuraSpell(const uint32 id, const uint32 attributes, const aura_type::Type aura, const int32 basePoints, const spell_effect_targets::Type target)
		{
			auto* spell = AddSpell(id, 0, attributes);
			spell->set_duration(attributes == spell_attributes::Passive ? -1 : 30000);
			auto* effect = spell->add_effects();
			effect->set_index(0);
			effect->set_type(spell_effects::ApplyAura);
			effect->set_aura(static_cast<uint32>(aura));
			effect->set_basepoints(basePoints);
			effect->set_targeta(target);
			return spell;
		}

		/// Casts a spell at the caster's feet (self target).
		SpellCastResult CastAtSelf(GamePlayerS& caster, const uint32 spellId)
		{
			return CastAt(caster, spellId, nullptr);
		}
	};
}

TEST_CASE_METHOD(AttackSpeedFixture, "ModAttackSpeed -15 slows a creature's swing to interval / 0.85", "[attack_speed_aura]")
{
	const auto player = SpawnPlayer(4);
	const auto creature = SpawnCreature(*rockUnit);
	const uint32 base = creature->GetAutoAttackTime(weapon_attack::BaseAttack);
	REQUIRE(base == 2000);

	REQUIRE(CastAt(*player, SlowAttackSpellId, creature.get()) == spell_cast_result::CastOkay);
	REQUIRE(RunUntil([&] { return creature->GetAutoAttackTime(weapon_attack::BaseAttack) != base; }));

	const uint32 expected = static_cast<uint32>(std::lround(2000.0 / 0.85));
	CHECK(creature->GetAutoAttackTime(weapon_attack::BaseAttack) == expected);
	CHECK(creature->GetAutoAttackTime(weapon_attack::OffhandAttack) == expected);
	CHECK(creature->Get<uint32>(object_fields::BaseAttackTime) == expected);

	REQUIRE(creature->RemoveAuraBySpellId(SlowAttackSpellId));
	CHECK(creature->GetAutoAttackTime(weapon_attack::BaseAttack) == 2000);
	CHECK(creature->Get<uint32>(object_fields::BaseAttackTime) == 2000);
}

TEST_CASE_METHOD(AttackSpeedFixture, "ModAttackSpeed +30 on the pet makes it swing faster until removed", "[attack_speed_aura]")
{
	const auto player = SpawnPlayer(4);
	player->SetStoredPets({ MakeStoredPet(ImpPetId) });
	GameCreatureS* pet = Summon(*player, ImpPetId);
	REQUIRE(pet->GetAutoAttackTime(weapon_attack::BaseAttack) == 2000);

	REQUIRE(CastAtSelf(*player, PetFrenzySpellId) == spell_cast_result::CastOkay);
	REQUIRE(RunUntil([&] { return pet->GetAutoAttackTime(weapon_attack::BaseAttack) != 2000; }));

	const uint32 expected = static_cast<uint32>(std::lround(2000.0 / 1.3));
	CHECK(pet->GetAutoAttackTime(weapon_attack::BaseAttack) == expected);
	CHECK(pet->Get<uint32>(object_fields::BaseAttackTime) == expected);

	REQUIRE(pet->RemoveAuraBySpellId(PetFrenzySpellId));
	CHECK(pet->GetAutoAttackTime(weapon_attack::BaseAttack) == 2000);
	CHECK(pet->Get<uint32>(object_fields::BaseAttackTime) == 2000);
}

TEST_CASE_METHOD(AttackSpeedFixture, "ModAttackSpeed scales both hands of a player and stacks additively", "[attack_speed_aura]")
{
	const auto player = SpawnPlayer(4);
	REQUIRE(player->GetAutoAttackTime(weapon_attack::BaseAttack) == 2000);

	REQUIRE(CastAtSelf(*player, SelfHasteSpellId) == spell_cast_result::CastOkay);
	const uint32 expected = static_cast<uint32>(std::lround(2000.0 / 1.3));
	CHECK(player->GetAutoAttackTime(weapon_attack::BaseAttack) == expected);
	CHECK(player->GetAutoAttackTime(weapon_attack::OffhandAttack) == expected);
	CHECK(player->Get<uint32>(object_fields::BaseAttackTime) == expected);

	// Equipment changes recompute the base time; the aura must survive a stat refresh
	player->RefreshStats();
	CHECK(player->Get<uint32>(object_fields::BaseAttackTime) == expected);

	REQUIRE(player->RemoveAuraBySpellId(SelfHasteSpellId));
	CHECK(player->GetAutoAttackTime(weapon_attack::BaseAttack) == 2000);
	CHECK(player->Get<uint32>(object_fields::BaseAttackTime) == 2000);
}

TEST_CASE("ModAttackSpeed auras add their percentages and never stall the swing", "[attack_speed_aura]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = std::make_shared<SwingUnit>(project, timers);
	unit->Initialize();
	proto::SpellEntry spell = test::MakeSpell();
	AuraContainer container{ *unit, 0, spell, 0, 0 };
	proto::SpellEffect effect;
	effect.set_aura(static_cast<uint32>(aura_type::ModAttackSpeed));

	AuraEffect haste(container, effect, timers, 30);
	AuraEffect slow(container, effect, timers, -15);
	haste.HandleEffect(true);
	slow.HandleEffect(true);
	CHECK(unit->GetAttackSpeedMultiplier() == Approx(1.15f));
	CHECK(unit->GetAutoAttackTime(weapon_attack::BaseAttack) == static_cast<uint32>(std::lround(2000.0 / 1.15)));

	slow.HandleEffect(false);
	haste.HandleEffect(false);
	CHECK(unit->GetAutoAttackTime(weapon_attack::BaseAttack) == 2000);

	// -100% or worse must not divide by zero or produce an endless interval
	AuraEffect cripple(container, effect, timers, -150);
	cripple.HandleEffect(true);
	const uint32 crippled = unit->GetAutoAttackTime(weapon_attack::BaseAttack);
	CHECK(crippled > 2000);
	CHECK(crippled <= 20000);
	cripple.HandleEffect(false);

	// Absurd haste must not drop the interval to zero
	AuraEffect frenzy(container, effect, timers, 100000);
	frenzy.HandleEffect(true);
	CHECK(unit->GetAutoAttackTime(weapon_attack::BaseAttack) > 0);
	frenzy.HandleEffect(false);
}

TEST_CASE("ModAttackSpeed applied mid-swing keeps the current swing and changes the next", "[attack_speed_aura]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto attacker = std::make_shared<SwingUnit>(project, timers);
	attacker->Initialize();
	attacker->Set<uint64>(object_fields::Guid, 1);
	auto victim = std::make_shared<SwingUnit>(project, timers);
	victim->Initialize();
	victim->Set<uint64>(object_fields::Guid, 2);

	attacker->BeginAttacking(victim);
	REQUIRE(attacker->IsAttackSwingArmed());
	const GameTime scheduled = attacker->GetNextAttackSwingTime();

	proto::SpellEntry spell = test::MakeSpell();
	AuraContainer container{ *attacker, 0, spell, 0, 0 };
	proto::SpellEffect effect;
	effect.set_aura(static_cast<uint32>(aura_type::ModAttackSpeed));
	AuraEffect haste(container, effect, timers, 100);
	haste.HandleEffect(true);

	// The swing already scheduled is not reset
	CHECK(attacker->IsAttackSwingArmed());
	CHECK(attacker->GetNextAttackSwingTime() == scheduled);

	// The next swing is scheduled with the hasted interval
	attacker->StopSwingForCast();
	const GameTime before = GetAsyncTimeMs();
	attacker->RaiseCastEnded(true);
	const GameTime after = GetAsyncTimeMs();
	REQUIRE(attacker->IsAttackSwingArmed());
	CHECK(attacker->GetNextAttackSwingTime() >= before + 1000);
	CHECK(attacker->GetNextAttackSwingTime() <= after + 1000);
}

TEST_CASE_METHOD(AttackSpeedFixture, "Ground zones live for the spell duration after Duration spell mods", "[ground_zone_duration]")
{
	const auto caster = SpawnPlayer(4);

	SECTION("Without the talent the zone lives for the spell duration")
	{
		const GameTime castAt = GetAsyncTimeMs();
		REQUIRE(CastAtSelf(*caster, ZoneSpellId) == spell_cast_result::CastOkay);
		REQUIRE(RunUntil([&] { return world->GetSpellZones().Size() == 1; }));
		const SpellZone& zone = world->GetSpellZones().GetZones().front();
		CHECK(zone.expiresAt >= castAt + 8000);
		CHECK(zone.expiresAt < castAt + 9000);
	}

	SECTION("A +4000 Duration mod makes the zone live 12 seconds")
	{
		caster->AddSpell(ZoneDurationTalentId);

		const GameTime castAt = GetAsyncTimeMs();
		REQUIRE(CastAtSelf(*caster, ZoneSpellId) == spell_cast_result::CastOkay);
		REQUIRE(RunUntil([&] { return world->GetSpellZones().Size() == 1; }));
		const SpellZone& zone = world->GetSpellZones().GetZones().front();
		CHECK(zone.expiresAt >= castAt + 12000);
		CHECK(zone.expiresAt < castAt + 13000);
	}
}
