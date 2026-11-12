// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// A spell that requires its target in front must not fail for a creature (an NPC or a pet) just
// because the target moved: creatures turn to face what they cast at, unless crowd control holds
// them. A pet's target moving past it during a cast threw the finished cast away. Players still
// have to face their target themselves.

#include "catch.hpp"
#include "persistent_pet_fixture.h"

#include <vector>

using namespace mmo;
using namespace mmo::pet_tests;

namespace
{
	constexpr float Pi = 3.14159265f;

	/// A grant spell that needs its target in front: its success is visible as a stored pet.
	proto::SpellEntry& AddFrontGrantSpell(proto::Project& project, const uint32 castTimeMs)
	{
		auto* spell = project.spells.add(302);
		spell->set_name("Bind Duskling");
		spell->add_attributes(0);
		spell->add_attributes(0);
		spell->set_casttime(castTimeMs);
		spell->set_facing(spell_facing_flags::TargetInFront);
		auto* effect = spell->add_effects();
		effect->set_index(0);
		effect->set_type(spell_effects::GrantPersistentPet);
		effect->set_targeta(spell_effect_targets::TargetAny);
		effect->set_miscvaluea(ScholarPetId);
		return *spell;
	}

	SpellTargetMap UnitTarget(const GameObjectS& target)
	{
		SpellTargetMap targetMap;
		targetMap.SetTargetMap(spell_cast_target_flags::Unit);
		targetMap.SetUnitTarget(target.GetGuid());
		return targetMap;
	}

	void TurnAway(GameUnitS& unit, const GameObjectS& from)
	{
		unit.SetFacing(unit.GetAngle(from) + Radian(Pi));
		REQUIRE_FALSE(unit.IsFacingTowards(from));
	}
}

TEST_CASE_METHOD(PersistentPetFixture, "a creature turns to an in-front target it was not facing", "[spell_facing]")
{
	const auto& spell = AddFrontGrantSpell(project, 0);
	const auto player = SpawnPlayer(4);
	const auto caster = SpawnCreature(*boarUnit);
	TurnAway(*caster, *player);

	REQUIRE(caster->CastSpell(UnitTarget(*player), spell, 0, false, 0, true) == spell_cast_result::CastOkay);
	CHECK(caster->IsFacingTowards(*player));
	CHECK(RunUntil([&] { return !player->GetStoredPets().empty(); }));
}

TEST_CASE_METHOD(PersistentPetFixture, "a creature's cast completes when its target moved behind it during the cast", "[spell_facing]")
{
	const auto& spell = AddFrontGrantSpell(project, 300);
	const auto player = SpawnPlayer(4);
	const auto caster = SpawnCreature(*boarUnit);
	caster->SetFacing(caster->GetAngle(*player));

	REQUIRE(caster->CastSpell(UnitTarget(*player), spell, 300, false, 0, true) == spell_cast_result::CastOkay);

	// The target walks past: the caster now looks the other way when the cast finishes.
	TurnAway(*caster, *player);

	CHECK(RunUntil([&] { return !player->GetStoredPets().empty(); }, std::chrono::milliseconds(3000)));
	CHECK(caster->IsFacingTowards(*player));
}

TEST_CASE_METHOD(PersistentPetFixture, "a stunned creature does not turn and its in-front cast fails", "[spell_facing]")
{
	const auto& spell = AddFrontGrantSpell(project, 0);
	const auto player = SpawnPlayer(4);
	const auto caster = SpawnCreature(*boarUnit);
	TurnAway(*caster, *player);
	caster->IncrementStunCount();
	caster->NotifyStunChanged();

	CHECK(caster->CastSpell(UnitTarget(*player), spell, 0, false, 0, true) != spell_cast_result::CastOkay);
	CHECK_FALSE(caster->IsFacingTowards(*player));
}

TEST_CASE_METHOD(PersistentPetFixture, "a player is not turned: an in-front cast at a target behind fails", "[spell_facing]")
{
	const auto& spell = AddFrontGrantSpell(project, 0);
	const auto caster = SpawnPlayer(4);
	const auto target = SpawnPlayer(4);
	target->Relocate(Vector3(3.0f, 0.0f, 0.0f), Radian(0.0f));
	TurnAway(*caster, *target);

	CHECK(caster->CastSpell(UnitTarget(*target), spell, 0, false, 0, true) == spell_cast_result::FailedUnitNotInfront);
	CHECK_FALSE(caster->IsFacingTowards(*target));
}

namespace
{
	/// Records the spell damage an owner is told about on behalf of the units it owns.
	class OwnerDamageWatcher final : public NetUnitWatcherS
	{
	public:
		struct Entry
		{
			uint64 source;
			uint64 target;
			uint32 amount;
		};
		std::vector<Entry> owned;
		uint32 own = 0;

		void OnOwnedUnitSpellDamageLog(const uint64 sourceGuid, const uint64 targetGuid, const uint32 amount, uint8, DamageFlags, const proto::SpellEntry&, uint32) override
		{
			owned.push_back({ sourceGuid, targetGuid, amount });
		}
		void OnSpellDamageLog(uint64, uint32, uint8, DamageFlags, const proto::SpellEntry&, uint32) override { ++own; }

		void OnAttackSwingEvent(AttackSwingEvent) override {}
		void OnTeleport(uint32, const Vector3&, const Radian&) override {}
		void OnXpLog(uint32) override {}
		void OnNonSpellDamageLog(uint64, uint32, DamageFlags) override {}
		void OnEnvironmentalDamageLog(uint64, uint32, EnvironmentalDamageType) override {}
		void OnSpeedChangeApplied(MovementType, float, uint32) override {}
		void OnRootChanged(bool, uint32) override {}
		void OnStunChanged(bool, uint32) override {}
		void OnSleepChanged(bool, uint32) override {}
		void OnFearChanged(bool, uint32) override {}
		void OnDisorientChanged(bool, uint32) override {}
		void OnPendingCharge(float, uint32) override {}
		void OnLevelUp(uint32, int32, int32, const std::array<int32, MaxAttributes>&, int32, int32) override {}
		void OnSpellModChanged(SpellModType, uint8, SpellModOp, int32) override {}
		void OnProficiencyChanged(uint32, bool) override {}
		void OnReviveOffer(uint64, uint32, uint32, uint32, const Vector3&, const Radian&) override {}
	};
}

TEST_CASE_METHOD(PersistentPetFixture, "spell damage of every unit a player owns reaches that player", "[pet_damage]")
{
	const auto& spell = AddFrontGrantSpell(project, 0);
	const auto player = SpawnPlayer(4);
	OwnerDamageWatcher ownerWatcher;
	player->SetNetUnitWatcher(&ownerWatcher);

	const auto pet = SpawnCreature(*boarUnit);
	pet->SetOwnerGuid(player->GetGuid());
	const auto swarm = SpawnCreature(*boarUnit, Vector3(4.0f, 0.0f, 0.0f));
	swarm->SetOwnerGuid(player->GetGuid());
	const auto petSummon = SpawnCreature(*boarUnit, Vector3(5.0f, 0.0f, 0.0f));
	petSummon->SetOwnerGuid(pet->GetGuid());
	const auto stranger = SpawnCreature(*boarUnit, Vector3(6.0f, 0.0f, 0.0f));

	pet->SpellDamageLog(stranger->GetGuid(), 10, 0, damage_flags::None, spell);
	swarm->SpellDamageLog(stranger->GetGuid(), 20, 0, damage_flags::None, spell);
	petSummon->SpellDamageLog(stranger->GetGuid(), 30, 0, damage_flags::None, spell);
	stranger->SpellDamageLog(pet->GetGuid(), 40, 0, damage_flags::None, spell);

	REQUIRE(ownerWatcher.owned.size() == 3);
	CHECK(ownerWatcher.owned[0].source == pet->GetGuid());
	CHECK(ownerWatcher.owned[0].amount == 10);
	CHECK(ownerWatcher.owned[1].source == swarm->GetGuid());
	CHECK(ownerWatcher.owned[2].source == petSummon->GetGuid());
	CHECK(ownerWatcher.own == 0);

	player->SpellDamageLog(stranger->GetGuid(), 50, 0, damage_flags::None, spell);
	CHECK(ownerWatcher.own == 1);
	CHECK(ownerWatcher.owned.size() == 3);

	player->SetNetUnitWatcher(nullptr);
}

namespace
{
	/// A faction nobody is an enemy or a friend of, like the Cellar Rats'.
	constexpr uint32 NeutralTemplate = 3;

	void AddNeutralFaction(proto::Project& project)
	{
		auto* neutral = project.factionTemplates.add(NeutralTemplate);
		neutral->set_name("Neutral Critters");
		neutral->set_flags(0);
		neutral->set_faction(3);
	}
}

TEST_CASE_METHOD(PersistentPetFixture, "a pet attacks a neutral creature its owner sends it at", "[pets][persistent]")
{
	AddNeutralFaction(project);
	proto::UnitEntry* critterEntry = AddUnitEntry(9100, "Cellar Rat", 2);
	critterEntry->set_factiontemplate(NeutralTemplate);
	const auto player = SpawnPlayer(4);
	player->SetStoredPets({ MakeStoredPet(ImpPetId) });
	GameCreatureS* pet = Summon(*player, ImpPetId);
	const auto rat = SpawnCreature(*critterEntry, Vector3(6.0f, 0.0f, 0.0f));
	REQUIRE_FALSE(player->UnitIsEnemy(*rat));
	REQUIRE(player->UnitCanAttack(*rat));

	SECTION("the Attack command")
	{
		player->GetPets().Command(pet_command::Attack, rat->GetGuid(), 0);
		CHECK(RunUntil([&] { return pet->GetVictim() == rat.get(); }));
	}

	SECTION("assisting the owner's harmful cast")
	{
		REQUIRE(CastAt(*player, BiteSpellId, rat.get()) == spell_cast_result::CastOkay);
		CHECK(RunUntil([&] { return pet->GetVictim() == rat.get(); }));
	}
}
