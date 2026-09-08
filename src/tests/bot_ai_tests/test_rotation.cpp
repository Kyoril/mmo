// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "bot_ai/bot_rotation.h"

#include "game/spell.h"
#include "proto_data/project.h"

using namespace mmo;

namespace
{
	/// Builds spell entries in code. The rotation is a rule about spell data, so testing it
	/// against the live spell table would couple the suite to whatever content says today.
	proto::SpellEntry MakeSpell(const uint32 id, const uint32 effectType, const int32 basePoints,
		const int32 dieSides = 0)
	{
		proto::SpellEntry spell;
		spell.set_id(id);
		spell.set_name("spell");
		spell.add_attributes(0);

		auto* effect = spell.add_effects();
		effect->set_index(0);
		effect->set_type(effectType);
		effect->set_basepoints(basePoints);
		effect->set_diesides(dieSides);
		return spell;
	}
}

TEST_CASE("A damaging spell is offensive", "[bot_ai][rotation]")
{
	const proto::SpellEntry spell = MakeSpell(1, spell_effects::SchoolDamage, 20);
	CHECK(IsOffensiveBotSpell(spell));
}

TEST_CASE("A heal is not something to open a fight with", "[bot_ai][rotation]")
{
	const proto::SpellEntry spell = MakeSpell(2, spell_effects::Heal, 50);
	CHECK_FALSE(IsOffensiveBotSpell(spell));
}

TEST_CASE("A passive spell is never cast", "[bot_ai][rotation]")
{
	proto::SpellEntry spell = MakeSpell(3, spell_effects::SchoolDamage, 20);
	spell.set_attributes(0, spell_attributes::Passive);
	CHECK_FALSE(IsOffensiveBotSpell(spell));
}

TEST_CASE("A spell flagged positive is not aimed at an enemy", "[bot_ai][rotation]")
{
	// Some beneficial spells carry a damaging effect - a buff that also burns undead, say - and
	// casting those at a creature wastes power and a global cooldown.
	proto::SpellEntry spell = MakeSpell(4, spell_effects::SchoolDamage, 20);
	spell.set_positive(1);
	CHECK_FALSE(IsOffensiveBotSpell(spell));
}

TEST_CASE("A weapon damage ability counts as offensive", "[bot_ai][rotation]")
{
	CHECK(IsOffensiveBotSpell(MakeSpell(5, spell_effects::WeaponDamage, 10)));
	CHECK(IsOffensiveBotSpell(MakeSpell(6, spell_effects::WeaponPercentDamage, 150)));
	CHECK(IsOffensiveBotSpell(MakeSpell(7, spell_effects::HealthLeech, 15)));
}

TEST_CASE("Damage is estimated from the mean roll, not the maximum", "[bot_ai][rotation]")
{
	// 20 base plus a 1-to-5 roll averages 20 + 3. Using the maximum would rank a wide low
	// spell above a narrow high one.
	const proto::SpellEntry spell = MakeSpell(8, spell_effects::SchoolDamage, 20, 5);
	CHECK(EstimateBotSpellDamage(spell) == Approx(23.0f));
}

TEST_CASE("A long cast is worth less than the same damage instantly", "[bot_ai][rotation]")
{
	proto::SpellEntry instant = MakeSpell(9, spell_effects::SchoolDamage, 60);
	proto::SpellEntry slow = MakeSpell(10, spell_effects::SchoolDamage, 60);
	slow.set_casttime(3000);

	// A bot is being hit while it casts, so damage per second of casting is the thing to
	// compare - not damage.
	CHECK(EstimateBotSpellDamage(instant) > EstimateBotSpellDamage(slow));
	CHECK(EstimateBotSpellDamage(slow) == Approx(20.0f));
}

TEST_CASE("Damage under a second is not inflated", "[bot_ai][rotation]")
{
	proto::SpellEntry quick = MakeSpell(11, spell_effects::SchoolDamage, 60);
	quick.set_casttime(500);

	// Dividing by half a second would make a fast spell look twice as good as an instant one
	// doing the same damage, which is backwards.
	CHECK(EstimateBotSpellDamage(quick) == Approx(60.0f));
}
