// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_client/combat_sound_resolver.h"

using namespace mmo;

namespace
{
	constexpr uint32 kMaterialPlate = 10;
	constexpr uint32 kMaterialFlesh = 11;

	constexpr uint32 kSubclassSword = 1;
	constexpr uint32 kSubclassShield = 2;
	constexpr uint32 kSubclassPlateArmor = 3;

	constexpr uint32 kModelHuman = 100;
	constexpr uint32 kModelBoar = 101;

	/// In-memory project slice holding just the two managers the resolver reads.
	struct ResolverFixture
	{
		proto_client::ItemSubclassManager subclasses;
		proto_client::ModelDataManager models;

		proto_client::ItemSubclassEntry& AddSubclass(const uint32 id)
		{
			auto* entry = subclasses.add(id);
			REQUIRE(entry != nullptr);
			entry->set_name("subclass");
			entry->set_itemclass(0);
			return *entry;
		}

		proto_client::ModelDataEntry& AddModel(const uint32 id)
		{
			auto* entry = models.add(id);
			REQUIRE(entry != nullptr);
			entry->set_name("model");
			entry->set_filename("model.hmsh");
			return *entry;
		}

		static void AddImpact(proto_client::ItemSubclassEntry& entry, const uint32 material, const uint32 sound)
		{
			auto* impact = entry.add_impact_sounds();
			impact->set_target_material(material);
			impact->set_sound(sound);
		}

		static void AddImpact(proto_client::CombatSounds& sounds, const uint32 material, const uint32 sound)
		{
			auto* impact = sounds.add_impact_sounds();
			impact->set_target_material(material);
			impact->set_sound(sound);
		}
	};

	CombatSwingOutcome MakeHit(const bool crit = false)
	{
		CombatSwingOutcome outcome;
		outcome.landed = true;
		outcome.crit = crit;
		return outcome;
	}
}

TEST_CASE("Victim material comes from worn chest armor", "[combat_sounds]")
{
	ResolverFixture fixture;
	fixture.AddSubclass(kSubclassPlateArmor).set_hit_material(kMaterialPlate);
	fixture.AddModel(kModelHuman).mutable_combat_sounds()->set_hit_material(kMaterialFlesh);

	CombatSoundVictim victim;
	victim.chestSubclass = kSubclassPlateArmor;
	victim.displayId = kModelHuman;

	CHECK(combat_sounds::ResolveVictimMaterial(fixture.subclasses, fixture.models, victim) == kMaterialPlate);
}

TEST_CASE("Victim material falls back to the model when no armor overrides it", "[combat_sounds]")
{
	ResolverFixture fixture;
	fixture.AddModel(kModelBoar).mutable_combat_sounds()->set_hit_material(kMaterialFlesh);

	CombatSoundVictim victim;
	victim.displayId = kModelBoar;

	CHECK(combat_sounds::ResolveVictimMaterial(fixture.subclasses, fixture.models, victim) == kMaterialFlesh);
}

TEST_CASE("Victim material is zero when neither armor nor model define one", "[combat_sounds]")
{
	ResolverFixture fixture;
	fixture.AddModel(kModelBoar);

	CombatSoundVictim victim;
	victim.displayId = kModelBoar;

	CHECK(combat_sounds::ResolveVictimMaterial(fixture.subclasses, fixture.models, victim) == 0);
}

TEST_CASE("Impact uses the row matching the victim material", "[combat_sounds]")
{
	ResolverFixture fixture;
	auto& sword = fixture.AddSubclass(kSubclassSword);
	ResolverFixture::AddImpact(sword, 0, 500);
	ResolverFixture::AddImpact(sword, kMaterialPlate, 501);
	fixture.AddSubclass(kSubclassPlateArmor).set_hit_material(kMaterialPlate);
	fixture.AddModel(kModelHuman);

	CombatSoundAttacker attacker;
	attacker.weaponSubclass = kSubclassSword;
	attacker.displayId = kModelHuman;

	CombatSoundVictim victim;
	victim.chestSubclass = kSubclassPlateArmor;
	victim.displayId = kModelHuman;

	const auto resolved = combat_sounds::ResolveSwingSounds(fixture.subclasses, fixture.models, attacker, victim, MakeHit());
	CHECK(resolved.impactSound == 501);
}

TEST_CASE("Impact falls back to the default row when no material matches", "[combat_sounds]")
{
	ResolverFixture fixture;
	auto& sword = fixture.AddSubclass(kSubclassSword);
	ResolverFixture::AddImpact(sword, 0, 500);
	ResolverFixture::AddImpact(sword, kMaterialPlate, 501);
	fixture.AddModel(kModelBoar).mutable_combat_sounds()->set_hit_material(kMaterialFlesh);

	CombatSoundAttacker attacker;
	attacker.weaponSubclass = kSubclassSword;
	attacker.displayId = kModelHuman;

	CombatSoundVictim victim;
	victim.displayId = kModelBoar;

	const auto resolved = combat_sounds::ResolveSwingSounds(fixture.subclasses, fixture.models, attacker, victim, MakeHit());
	CHECK(resolved.impactSound == 500);
}

TEST_CASE("An unarmed attacker uses its model's natural weapon sounds", "[combat_sounds]")
{
	ResolverFixture fixture;
	auto* boarSounds = fixture.AddModel(kModelBoar).mutable_combat_sounds();
	boarSounds->set_swing_sound(600);
	ResolverFixture::AddImpact(*boarSounds, 0, 601);

	CombatSoundAttacker attacker;
	attacker.displayId = kModelBoar;

	CombatSoundVictim victim;

	CHECK(combat_sounds::ResolveSwingSound(fixture.subclasses, fixture.models, attacker) == 600);

	const auto resolved = combat_sounds::ResolveSwingSounds(fixture.subclasses, fixture.models, attacker, victim, MakeHit());
	CHECK(resolved.impactSound == 601);
}

TEST_CASE("A weapon without sounds falls back to the attacker model", "[combat_sounds]")
{
	ResolverFixture fixture;
	fixture.AddSubclass(kSubclassSword);
	auto* humanSounds = fixture.AddModel(kModelHuman).mutable_combat_sounds();
	humanSounds->set_swing_sound(610);
	ResolverFixture::AddImpact(*humanSounds, 0, 611);

	CombatSoundAttacker attacker;
	attacker.weaponSubclass = kSubclassSword;
	attacker.displayId = kModelHuman;

	CombatSoundVictim victim;

	CHECK(combat_sounds::ResolveSwingSound(fixture.subclasses, fixture.models, attacker) == 610);

	const auto resolved = combat_sounds::ResolveSwingSounds(fixture.subclasses, fixture.models, attacker, victim, MakeHit());
	CHECK(resolved.impactSound == 611);
}

TEST_CASE("A critical hit layers the crit sound on top of the impact", "[combat_sounds]")
{
	ResolverFixture fixture;
	auto& sword = fixture.AddSubclass(kSubclassSword);
	ResolverFixture::AddImpact(sword, 0, 500);
	sword.set_crit_layer_sound(502);
	fixture.AddModel(kModelHuman);

	CombatSoundAttacker attacker;
	attacker.weaponSubclass = kSubclassSword;
	attacker.displayId = kModelHuman;

	CombatSoundVictim victim;
	victim.displayId = kModelHuman;

	const auto resolved = combat_sounds::ResolveSwingSounds(fixture.subclasses, fixture.models, attacker, victim, MakeHit(true));
	CHECK(resolved.impactSound == 500);
	CHECK(resolved.critLayerSound == 502);
}

TEST_CASE("A miss plays the attacker's whiff and no impact", "[combat_sounds]")
{
	ResolverFixture fixture;
	auto& sword = fixture.AddSubclass(kSubclassSword);
	ResolverFixture::AddImpact(sword, 0, 500);
	sword.set_miss_sound(503);
	fixture.AddModel(kModelHuman);

	CombatSoundAttacker attacker;
	attacker.weaponSubclass = kSubclassSword;
	attacker.displayId = kModelHuman;

	CombatSoundVictim victim;
	victim.displayId = kModelHuman;

	CombatSwingOutcome outcome;
	outcome.miss = true;

	const auto resolved = combat_sounds::ResolveSwingSounds(fixture.subclasses, fixture.models, attacker, victim, outcome);
	CHECK(resolved.missSound == 503);
	CHECK(resolved.impactSound == 0);
}

TEST_CASE("A parry sound comes from the victim's main hand, not the attacker's", "[combat_sounds]")
{
	ResolverFixture fixture;
	auto& attackerSword = fixture.AddSubclass(kSubclassSword);
	attackerSword.set_miss_sound(503);
	attackerSword.set_parry_sound(504);

	constexpr uint32 kSubclassAxe = 4;
	fixture.AddSubclass(kSubclassAxe).set_parry_sound(505);
	fixture.AddModel(kModelHuman);

	CombatSoundAttacker attacker;
	attacker.weaponSubclass = kSubclassSword;
	attacker.displayId = kModelHuman;

	CombatSoundVictim victim;
	victim.mainHandSubclass = kSubclassAxe;
	victim.displayId = kModelHuman;

	CombatSwingOutcome outcome;
	outcome.parry = true;

	const auto resolved = combat_sounds::ResolveSwingSounds(fixture.subclasses, fixture.models, attacker, victim, outcome);
	CHECK(resolved.parrySound == 505);
}

TEST_CASE("A parry without a victim weapon sound falls back to the attacker's whiff", "[combat_sounds]")
{
	ResolverFixture fixture;
	fixture.AddSubclass(kSubclassSword).set_miss_sound(503);
	fixture.AddModel(kModelHuman);

	CombatSoundAttacker attacker;
	attacker.weaponSubclass = kSubclassSword;
	attacker.displayId = kModelHuman;

	CombatSoundVictim victim;
	victim.displayId = kModelHuman;

	CombatSwingOutcome outcome;
	outcome.parry = true;

	const auto resolved = combat_sounds::ResolveSwingSounds(fixture.subclasses, fixture.models, attacker, victim, outcome);
	CHECK(resolved.parrySound == 503);
}

TEST_CASE("A full block replaces the impact, a partial block layers over it", "[combat_sounds]")
{
	ResolverFixture fixture;
	auto& sword = fixture.AddSubclass(kSubclassSword);
	ResolverFixture::AddImpact(sword, 0, 500);
	fixture.AddSubclass(kSubclassShield).set_block_sound(506);
	fixture.AddModel(kModelHuman);

	CombatSoundAttacker attacker;
	attacker.weaponSubclass = kSubclassSword;
	attacker.displayId = kModelHuman;

	CombatSoundVictim victim;
	victim.offHandSubclass = kSubclassShield;
	victim.displayId = kModelHuman;

	CombatSwingOutcome fullBlock;
	fullBlock.block = true;
	fullBlock.fullBlock = true;

	const auto blocked = combat_sounds::ResolveSwingSounds(fixture.subclasses, fixture.models, attacker, victim, fullBlock);
	CHECK(blocked.blockSound == 506);
	CHECK(blocked.impactSound == 0);

	CombatSwingOutcome partialBlock;
	partialBlock.block = true;
	partialBlock.landed = true;

	const auto partial = combat_sounds::ResolveSwingSounds(fixture.subclasses, fixture.models, attacker, victim, partialBlock);
	CHECK(partial.blockSound == 506);
	CHECK(partial.impactSound == 500);
}

TEST_CASE("Voice resolution picks the crit variant only on a crit", "[combat_sounds]")
{
	ResolverFixture fixture;
	auto* sounds = fixture.AddModel(kModelHuman).mutable_combat_sounds();
	sounds->set_attack_voice_sound(700);
	sounds->set_attack_voice_chance(20);
	sounds->set_attack_crit_voice_sound(701);
	sounds->set_attack_crit_voice_chance(100);
	sounds->set_hit_voice_sound(702);
	sounds->set_hit_voice_chance(80);
	sounds->set_crit_hit_voice_sound(703);

	const auto normalAttack = combat_sounds::ResolveAttackVoice(fixture.models, kModelHuman, false);
	CHECK(normalAttack.sound == 700);
	CHECK(normalAttack.chance == 20);

	const auto critAttack = combat_sounds::ResolveAttackVoice(fixture.models, kModelHuman, true);
	CHECK(critAttack.sound == 701);
	CHECK(critAttack.chance == 100);

	const auto normalHit = combat_sounds::ResolveHitVoice(fixture.models, kModelHuman, false);
	CHECK(normalHit.sound == 702);
	CHECK(normalHit.chance == 80);

	const auto critHit = combat_sounds::ResolveHitVoice(fixture.models, kModelHuman, true);
	CHECK(critHit.sound == 703);
}

TEST_CASE("A crit without an authored crit voice uses the normal voice", "[combat_sounds]")
{
	ResolverFixture fixture;
	auto* sounds = fixture.AddModel(kModelHuman).mutable_combat_sounds();
	sounds->set_attack_voice_sound(700);
	sounds->set_attack_voice_chance(20);

	const auto critAttack = combat_sounds::ResolveAttackVoice(fixture.models, kModelHuman, true);
	CHECK(critAttack.sound == 700);
	CHECK(critAttack.chance == 20);
}

TEST_CASE("An unknown model yields no voice", "[combat_sounds]")
{
	ResolverFixture fixture;

	const auto voice = combat_sounds::ResolveAttackVoice(fixture.models, kModelHuman, false);
	CHECK(voice.sound == 0);
	CHECK(voice.chance == 0);
}
