// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot/bot_spell_info.h"

#include "game/spell.h"

#include "catch.hpp"

using namespace mmo;

namespace
{
	constexpr uint32 FortyYards = 4;

	/// Builds spell entries in code, like the headless bot's rotation tests, so the suite does
	/// not depend on today's spell data.
	proto_client::SpellEntry MakeSpell(const uint32 id, const uint32 effectType, const int32 basePoints)
	{
		proto_client::SpellEntry spell;
		spell.set_id(id);
		spell.set_name("spell");
		spell.add_attributes(0);

		auto* effect = spell.add_effects();
		effect->set_index(0);
		effect->set_type(effectType);
		effect->set_basepoints(basePoints);
		return spell;
	}

	void AddFortyYards(proto_client::RangeManager& ranges)
	{
		ranges.add(FortyYards)->set_range(40.0f);
	}
}

TEST_CASE("A passive damaging spell is not offensive", "[bot_spell_info]")
{
	proto_client::RangeManager ranges;
	AddFortyYards(ranges);

	proto_client::SpellEntry spell = MakeSpell(1, spell_effects::SchoolDamage, 20);
	spell.set_attributes(0, spell_attributes::Passive);
	CHECK_FALSE(DescribeBotSpell(spell, ranges).offensive);
}

TEST_CASE("A positive spell with a damage effect is not offensive", "[bot_spell_info]")
{
	proto_client::RangeManager ranges;
	AddFortyYards(ranges);

	proto_client::SpellEntry spell = MakeSpell(2, spell_effects::SchoolDamage, 20);
	spell.set_positive(1);
	CHECK_FALSE(DescribeBotSpell(spell, ranges).offensive);
}

TEST_CASE("A school damage spell is offensive and takes its range from its range type", "[bot_spell_info]")
{
	proto_client::RangeManager ranges;
	AddFortyYards(ranges);

	proto_client::SpellEntry spell = MakeSpell(3, spell_effects::SchoolDamage, 20);
	spell.set_rangetype(FortyYards);
	spell.set_casttime(1500);
	spell.set_cost(30);
	spell.set_powertype(power_type::Mana);

	const BotSpellInfo info = DescribeBotSpell(spell, ranges);
	CHECK(info.offensive);
	CHECK(info.maxRange == Approx(40.0f));
	CHECK(info.castTime == 1500);
	CHECK(info.cost == 30);
	CHECK(info.powerType == power_type::Mana);
}

TEST_CASE("A spell without a range type has no range limit", "[bot_spell_info]")
{
	proto_client::RangeManager ranges;
	AddFortyYards(ranges);

	CHECK(DescribeBotSpell(MakeSpell(4, spell_effects::SchoolDamage, 20), ranges).maxRange == 0.0f);
}

TEST_CASE("The score ranks an instant above a slow cast with more damage", "[bot_spell_info]")
{
	proto_client::RangeManager ranges;
	AddFortyYards(ranges);

	const proto_client::SpellEntry instant = MakeSpell(5, spell_effects::SchoolDamage, 40);
	proto_client::SpellEntry slow = MakeSpell(6, spell_effects::SchoolDamage, 60);
	slow.set_casttime(3000);

	CHECK(DescribeBotSpell(instant, ranges).score > DescribeBotSpell(slow, ranges).score);
}
