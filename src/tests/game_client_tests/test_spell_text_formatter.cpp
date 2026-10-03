// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "game_client/spell_text_formatter.h"

#include "game/aura.h"
#include "game/spell.h"

#include <map>

using namespace mmo;

namespace
{
	/// A Fire Barrage shaped pair: a 1.5 s channel ticking every 0.5 s, each tick triggering
	/// a projectile spell that deals 19-23 fire damage at its base level.
	struct SpellFixture
	{
		std::map<uint32, proto_client::SpellEntry> spells;
		SpellTextContext context;

		SpellFixture()
		{
			proto_client::SpellEntry& channel = spells[150];
			channel.set_id(150);
			channel.set_name("Fire Barrage");
			channel.set_duration(1500);
			channel.set_baselevel(4);
			channel.set_spelllevel(4);
			channel.set_maxlevel(10);
			auto* trigger = channel.add_effects();
			trigger->set_index(0);
			trigger->set_type(spell_effects::ApplyAura);
			trigger->set_aura(aura_type::PeriodicTriggerSpell);
			trigger->set_amplitude(500);
			trigger->set_triggerspell(152);
			auto* dummy = channel.add_effects();
			dummy->set_index(1);
			dummy->set_type(spell_effects::ApplyAura);
			dummy->set_aura(aura_type::Dummy);

			proto_client::SpellEntry& projectile = spells[152];
			projectile.set_id(152);
			projectile.set_name("Fire Barrage Projectile");
			projectile.set_baselevel(4);
			projectile.set_spelllevel(4);
			projectile.set_maxlevel(10);
			auto* damage = projectile.add_effects();
			damage->set_index(0);
			damage->set_type(spell_effects::SchoolDamage);
			damage->set_basepoints(19);
			damage->set_diesides(4);

			context.level = 4;
			context.findSpell = [this](const uint32 id) -> const proto_client::SpellEntry*
			{
				const auto it = spells.find(id);
				return it == spells.end() ? nullptr : &it->second;
			};
			context.formatDuration = [](const std::string& key, const double value)
			{
				return key + ":" + std::to_string(static_cast<int>(value * 10.0));
			};
		}

		std::string Format(const std::string& text, const uint32 spellId = 150) const
		{
			return FormatSpellText(text, spells.at(spellId), context);
		}
	};
}

TEST_CASE("Spell text keeps the existing tokens working", "[spell_text]")
{
	const SpellFixture fixture;

	CHECK(fixture.Format("Deals $s0 damage.", 152) == "Deals 19 - 23 damage.");
	CHECK(fixture.Format("$m0 to $M0", 152) == "19 to 23");
	CHECK(fixture.Format("over $D", 150) == "over FORMAT_DURATION_SECONDS:15");
	CHECK(fixture.Format("over $d", 150) == "over FORMAT_DURATION_SECONDS_PRECISE:15");
	CHECK(fixture.Format("every $i0", 150) == "every FORMAT_DURATION_SECONDS_PRECISE:5");
}

TEST_CASE("Spell text reads values of another spell by id", "[spell_text]")
{
	const SpellFixture fixture;

	CHECK(fixture.Format("each wave deals $152s0 fire damage") == "each wave deals 19 - 23 fire damage");
	CHECK(fixture.Format("$152m0/$152M0") == "19/23");

	// The prefix only redirects one placeholder; the next one reads the own spell again
	CHECK(fixture.Format("$152s0 every $i0") == "19 - 23 every FORMAT_DURATION_SECONDS_PRECISE:5");
}

TEST_CASE("Spell text counts the ticks of a periodic effect", "[spell_text]")
{
	const SpellFixture fixture;

	CHECK(fixture.Format("$t0 waves") == "3 waves");
	CHECK(fixture.Format("$t1") == "0");
	CHECK(GetSpellEffectTickCount(fixture.spells.at(150), 0) == 3);
}

TEST_CASE("A periodic trigger totals the triggered spell's damage over all ticks", "[spell_text]")
{
	const SpellFixture fixture;

	CHECK(fixture.Format("$o0 in total") == "57 - 69 in total");
}

TEST_CASE("Referenced spells scale with the reader's level, clamped to their own range", "[spell_text]")
{
	SpellFixture fixture;

	auto* damage = fixture.spells[152].mutable_effects(0);
	damage->set_pointsperlevel(1.0f);

	fixture.context.level = 6;
	CHECK(fixture.Format("$152m0") == "21");

	fixture.context.level = 60;
	CHECK(fixture.Format("$152m0") == "25");
}

TEST_CASE("Malformed placeholders stay visible instead of vanishing", "[spell_text]")
{
	const SpellFixture fixture;

	CHECK(fixture.Format("costs $$5") == "costs $5");
	CHECK(fixture.Format("$999s0 damage") == "$999s0 damage");
	CHECK(fixture.Format("$q and $152q0") == "$q and $152q0");
	CHECK(fixture.Format("ends with $") == "ends with $");
	CHECK(fixture.Format("ends with $152") == "ends with $152");
}

TEST_CASE("A non-digit after a token is not read as an effect index", "[spell_text]")
{
	const SpellFixture fixture;

	// "$s." used to parse '.' as effect index -2
	CHECK(fixture.Format("$s0, then $m.", 152) == "19 - 23, then 19.");
}
