// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "game_client/spell_text_formatter.h"

#include "game/aura.h"
#include "game/spell.h"

#include <cmath>
#include <limits>
#include <map>
#include <iterator>
#include <random>
#include <vector>

using namespace mmo;

namespace
{
	constexpr int32 Int32Max = std::numeric_limits<int32>::max();
	constexpr int32 Int32Min = std::numeric_limits<int32>::min();

	proto_client::SpellEffect& AddEffect(proto_client::SpellEntry& spell, const uint32 type, const uint32 aura = 0)
	{
		proto_client::SpellEffect& effect = *spell.add_effects();
		effect.set_index(static_cast<uint32>(spell.effects_size() - 1));
		effect.set_type(type);
		effect.set_aura(aura);
		return effect;
	}

	proto_client::SpellEffect& AddDamage(proto_client::SpellEntry& spell, const int32 basePoints, const int32 dieSides = 0)
	{
		proto_client::SpellEffect& effect = AddEffect(spell, spell_effects::SchoolDamage);
		effect.set_basepoints(basePoints);
		effect.set_diesides(dieSides);
		return effect;
	}

	proto_client::SpellEffect& AddPeriodic(proto_client::SpellEntry& spell, const uint32 aura, const int32 amplitude)
	{
		proto_client::SpellEffect& effect = AddEffect(spell, spell_effects::ApplyAura, aura);
		effect.set_amplitude(amplitude);
		return effect;
	}

	/// A small spell catalog covering every shape the formatter distinguishes. Spells 150 and
	/// 152 have the shape of the live Fire Barrage pair: a 1.5 s channel ticking every 0.5 s,
	/// each tick casting a projectile for 19 - 23 fire damage at its base level.
	struct SpellFixture
	{
		std::map<uint32, proto_client::SpellEntry> spells;
		std::map<std::string, std::string> durationFormats = {
			{ "FORMAT_DURATION_SECONDS", "%.0f sec" },
			{ "FORMAT_DURATION_SECONDS_PRECISE", "%.2f sec" },
			{ "FORMAT_DURATION_MINUTES", "%.0f min" },
			{ "FORMAT_DURATION_MINUTES_PRECISE", "%.2f min" },
			{ "FORMAT_DURATION_HOURS", "%.0f h" },
			{ "FORMAT_DURATION_HOURS_PRECISE", "%.2f h" },
		};
		SpellTextContext context;

		proto_client::SpellEntry& Spell(const uint32 id, const int32 duration = 0)
		{
			proto_client::SpellEntry& spell = spells[id];
			spell.set_id(id);
			spell.set_name("Spell " + std::to_string(id));
			spell.set_duration(duration);
			return spell;
		}

		SpellFixture()
		{
			// Fire Barrage and its projectile
			proto_client::SpellEntry& channel = Spell(150, 1500);
			channel.set_baselevel(4);
			channel.set_spelllevel(4);
			channel.set_maxlevel(10);
			AddPeriodic(channel, aura_type::PeriodicTriggerSpell, 500).set_triggerspell(152);
			AddEffect(channel, spell_effects::ApplyAura, aura_type::Dummy);

			proto_client::SpellEntry& projectile = Spell(152);
			projectile.set_baselevel(4);
			projectile.set_spelllevel(4);
			projectile.set_maxlevel(10);
			AddDamage(projectile, 19, 4);

			// A damage over time: 10 every 3 s for 12 s
			AddPeriodic(Spell(160, 12000), aura_type::PeriodicDamage, 3000).set_basepoints(10);

			// Three effects: a flat value, a range and a dummy
			proto_client::SpellEntry& multi = Spell(170, 8000);
			AddDamage(multi, 5);
			AddDamage(multi, 7, 2);
			AddEffect(multi, spell_effects::ApplyAura, aura_type::Dummy);

			// Periodic triggers in every shape the total has to cope with
			AddPeriodic(Spell(180, 2000), aura_type::PeriodicTriggerSpell, 1000).set_triggerspell(180);
			AddPeriodic(Spell(181, 2000), aura_type::PeriodicTriggerSpell, 1000).set_triggerspell(999);
			AddPeriodic(Spell(182, 2000), aura_type::PeriodicTriggerSpell, 1000).set_triggerspell(190);
			AddPeriodic(Spell(183, 3000), aura_type::PeriodicTriggerSpell, 1000).set_triggerspell(201);
			AddPeriodic(Spell(184, 2000), aura_type::PeriodicTriggerSpell, 1000).set_triggerspell(191);
			AddPeriodic(Spell(185, 2000), aura_type::PeriodicTriggerSpell, 1000);

			AddEffect(Spell(190), spell_effects::ApplyAura, aura_type::Dummy);

			proto_client::SpellEntry& dummyThenDamage = Spell(191);
			AddEffect(dummyThenDamage, spell_effects::ApplyAura, aura_type::Dummy);
			AddDamage(dummyThenDamage, 7);

			AddEffect(Spell(201), spell_effects::Heal).set_basepoints(15);

			context.level = 4;
			context.findSpell = [this](const uint32 id) -> const proto_client::SpellEntry*
			{
				const auto it = spells.find(id);
				return it == spells.end() ? nullptr : &it->second;
			};
			context.findDurationFormat = [this](const std::string& key) -> const std::string*
			{
				const auto it = durationFormats.find(key);
				return it == durationFormats.end() ? nullptr : &it->second;
			};
		}

		std::string Format(const std::string& text, const uint32 spellId) const
		{
			return FormatSpellText(text, spells.at(spellId), context);
		}
	};

	std::string FormatValue(const std::string& format, const double value)
	{
		std::string out;
		AppendFormattedValue(out, format, value);
		return out;
	}
}

TEST_CASE("Text without placeholders is returned unchanged", "[spell_text]")
{
	const SpellFixture fixture;

	CHECK(fixture.Format("", 170).empty());
	CHECK(fixture.Format("Hurls a fireball.", 170) == "Hurls a fireball.");

	// Multibyte UTF-8 passes through untouched, including next to placeholders
	CHECK(fixture.Format("Verursacht Schaden über Zeit.", 170) == "Verursacht Schaden über Zeit.");
	CHECK(fixture.Format("Наносит $s0 ед. урона", 170) == "Наносит 5 ед. урона");
	CHECK(fixture.Format("dégâts: $s0 – fin", 170) == "dégâts: 5 – fin");
}

TEST_CASE("Point tokens write minimum, maximum and range", "[spell_text]")
{
	const SpellFixture fixture;

	CHECK(fixture.Format("$s0", 152) == "19 - 23");
	CHECK(fixture.Format("$S0", 152) == "19 - 23");
	CHECK(fixture.Format("$m0", 152) == "19");
	CHECK(fixture.Format("$M0", 152) == "23");

	// A fixed value prints once instead of "5 - 5"
	CHECK(fixture.Format("$s0", 170) == "5");
	CHECK(fixture.Format("$m0 $M0", 170) == "5 5");
}

TEST_CASE("Point tokens print absolute values and honour base dice", "[spell_text]")
{
	SpellFixture fixture;
	proto_client::SpellEntry& spell = fixture.Spell(300);
	AddDamage(spell, -10);
	proto_client::SpellEffect& dice = AddDamage(spell, 10, 5);
	dice.set_basedice(1);

	CHECK(fixture.Format("$s0", 300) == "10");
	CHECK(fixture.Format("$s1", 300) == "11 - 15");
}

TEST_CASE("Effect indices select, persist and stay in range", "[spell_text]")
{
	const SpellFixture fixture;

	CHECK(fixture.Format("$s0 / $s1", 170) == "5 / 7 - 9");

	// An omitted index reuses the previous one, as authored descriptions rely on
	CHECK(fixture.Format("$s1 and $m", 170) == "7 - 9 and 7");
	CHECK(fixture.Format("$s", 170) == "5");

	// Out of range effects read as zero instead of reading past the effect list
	CHECK(fixture.Format("$s3", 170) == "0");
	CHECK(fixture.Format("$s9 $m9 $M9 $o9 $t9 $i9", 170) == "0 0 0 0 0 0.00 sec");

	// A dummy effect has no points
	CHECK(fixture.Format("$s2", 170) == "0");

	// A non-digit after a token is text, not an index ("$m." once parsed '.' as -2)
	CHECK(fixture.Format("$s1, then $m.", 170) == "7 - 9, then 7.");
	CHECK(fixture.Format("$sx", 170) == "5x");

	// Only one index digit is read; the next digit is text
	CHECK(fixture.Format("$s01", 170) == "51");
}

TEST_CASE("Tokens without an index do not swallow a following digit", "[spell_text]")
{
	const SpellFixture fixture;

	CHECK(fixture.Format("$D0", 160) == "12 sec0");
	CHECK(fixture.Format("$d5", 152) == "0.00 sec5");
}

TEST_CASE("Effect points scale with the reader's level within the spell's level range", "[spell_text]")
{
	SpellFixture fixture;
	proto_client::SpellEntry& spell = fixture.Spell(310);
	spell.set_baselevel(4);
	spell.set_spelllevel(4);
	spell.set_maxlevel(10);
	proto_client::SpellEffect& effect = AddDamage(spell, 19, 4);
	effect.set_pointsperlevel(2.0f);
	effect.set_diceperlevel(1.5f);

	// Below the base level counts as the base level
	fixture.context.level = 1;
	CHECK(fixture.Format("$s0", 310) == "19 - 23");

	// Three levels above: +6 base points, dice 4 + 4.5 truncated to 8
	fixture.context.level = 7;
	CHECK(fixture.Format("$s0", 310) == "25 - 33");

	// Above the max level counts as the max level
	fixture.context.level = 60;
	CHECK(fixture.Format("$s0", 310) == "31 - 44");

	// Max level 0 means uncapped
	spell.set_maxlevel(0);
	CHECK(fixture.Format("$m0", 310) == "131");
}

TEST_CASE("Total tokens multiply periodic effects by their tick count", "[spell_text]")
{
	const SpellFixture fixture;

	CHECK(fixture.Format("$o0", 160) == "40");
	CHECK(fixture.Format("$O0", 160) == "40");

	// Not periodic: the total is the plain value
	CHECK(fixture.Format("$o0 $o1", 170) == "5 7 - 9");
}

TEST_CASE("A periodic trigger totals the triggered spell's damage or healing", "[spell_text]")
{
	const SpellFixture fixture;

	// Fire Barrage: three waves of 19 - 23
	CHECK(fixture.Format("$o0", 150) == "57 - 69");

	// Healing counts, three ticks of 15
	CHECK(fixture.Format("$o0", 183) == "45");

	// The first damage or heal effect is used, skipping a leading dummy
	CHECK(fixture.Format("$o0", 184) == "14");
}

TEST_CASE("A periodic trigger without a usable target spell falls back to its own points", "[spell_text]")
{
	SpellFixture fixture;

	CHECK(fixture.Format("$o0", 180) == "0");   // triggers itself
	CHECK(fixture.Format("$o0", 181) == "0");   // triggers an unknown spell
	CHECK(fixture.Format("$o0", 182) == "0");   // triggered spell deals nothing
	CHECK(fixture.Format("$o0", 185) == "0");   // triggers nothing

	fixture.context.findSpell = nullptr;
	CHECK(fixture.Format("$o0", 150) == "0");
}

TEST_CASE("Tick counts only exist for periodic effects with amplitude and duration", "[spell_text]")
{
	SpellFixture fixture;

	CHECK(fixture.Format("$t0", 150) == "3");
	CHECK(fixture.Format("$T0", 160) == "4");
	CHECK(fixture.Format("$t1", 150) == "0");
	CHECK(fixture.Format("$t0", 170) == "0");

	proto_client::SpellEntry& noAmplitude = fixture.Spell(320, 5000);
	AddPeriodic(noAmplitude, aura_type::PeriodicDamage, 0);
	AddPeriodic(noAmplitude, aura_type::PeriodicDamage, -1000);
	CHECK(fixture.Format("$t0 $t1", 320) == "0 0");

	AddPeriodic(fixture.Spell(321, 0), aura_type::PeriodicDamage, 1000);
	AddPeriodic(fixture.Spell(322, -5000), aura_type::PeriodicDamage, 1000);
	CHECK(fixture.Format("$t0", 321) == "0");
	CHECK(fixture.Format("$t0", 322) == "0");

	// An amplitude that does not divide the duration rounds down, like the server
	AddPeriodic(fixture.Spell(323, 5000), aura_type::PeriodicHeal, 2000);
	CHECK(fixture.Format("$t0", 323) == "2");

	CHECK(GetSpellEffectTickCount(fixture.spells.at(150), -1) == 0);
	CHECK(GetSpellEffectTickCount(fixture.spells.at(150), 2) == 0);
}

TEST_CASE("Durations pick seconds, minutes or hours", "[spell_text]")
{
	SpellFixture fixture;
	const auto duration = [&fixture](const int32 milliseconds, const char* text)
	{
		fixture.Spell(330, milliseconds);
		return fixture.Format(text, 330);
	};

	CHECK(duration(1500, "$d") == "1.50 sec");
	CHECK(duration(2000, "$D") == "2 sec");
	CHECK(duration(2000, "$d") == "2.00 sec");
	CHECK(duration(0, "$D") == "0 sec");
	CHECK(duration(59999, "$D") == "60 sec");
	CHECK(duration(59999, "$d") == "60.00 sec");
	CHECK(duration(60000, "$D") == "1 min");
	CHECK(duration(90000, "$d") == "1.50 min");
	CHECK(duration(3599999, "$D") == "60 min");
	CHECK(duration(3600000, "$D") == "1 h");
	CHECK(duration(5400000, "$d") == "1.50 h");
	CHECK(duration(Int32Max, "$D") == "596.52 h");
	CHECK(duration(-1000, "$D") == "-1 sec");
}

TEST_CASE("Rounded durations keep fractions instead of rounding them away", "[spell_text]")
{
	SpellFixture fixture;
	const auto duration = [&fixture](const int32 milliseconds)
	{
		fixture.Spell(331, milliseconds);
		return fixture.Format("$D", 331);
	};

	// A 1.5 s channel once read "over 2 seconds" while the cast line said 1.5
	CHECK(duration(1500) == "1.5 sec");
	CHECK(duration(1250) == "1.25 sec");
	CHECK(duration(500) == "0.5 sec");
	CHECK(duration(10000) == "10 sec");
	CHECK(duration(90000) == "1.5 min");
	CHECK(duration(5400000) == "1.5 h");
	CHECK(duration(-1500) == "-1.5 sec");

	// Within the precise templates' two decimals a value counts as whole
	CHECK(duration(1001) == "1 sec");
	CHECK(duration(1006) == "1.01 sec");
	CHECK(duration(59999) == "60 sec");
}

TEST_CASE("Tick intervals are formatted like durations", "[spell_text]")
{
	SpellFixture fixture;

	CHECK(fixture.Format("$i0", 150) == "0.50 sec");
	CHECK(fixture.Format("$I0", 150) == "0.5 sec");
	CHECK(fixture.Format("$I0", 160) == "3 sec");
	CHECK(fixture.Format("$i0", 170) == "0.00 sec");

	AddPeriodic(fixture.Spell(340, 600000), aura_type::PeriodicDamage, -500);
	CHECK(fixture.Format("$I0", 340) == "0 sec");
}

TEST_CASE("Missing duration formats write the localization key", "[spell_text]")
{
	SpellFixture fixture;

	fixture.durationFormats.erase("FORMAT_DURATION_SECONDS_PRECISE");
	CHECK(fixture.Format("$d", 150) == "FORMAT_DURATION_SECONDS_PRECISE");
	CHECK(fixture.Format("$D", 150) == "FORMAT_DURATION_SECONDS_PRECISE");
	CHECK(fixture.Format("$D", 160) == "12 sec");

	fixture.context.findDurationFormat = nullptr;
	CHECK(fixture.Format("$D", 160) == "FORMAT_DURATION_SECONDS");
}

TEST_CASE("Durations, ticks and totals follow the reader's duration modifiers", "[spell_text]")
{
	SpellFixture fixture;

	// Like a talent adding 50% duration to spell 160 only; infinite durations stay infinite
	std::vector<uint32> queried;
	fixture.context.getDuration = [&queried](const proto_client::SpellEntry& spell)
	{
		queried.push_back(spell.id());
		return spell.id() == 160 ? spell.duration() * 3 / 2 : spell.duration();
	};

	CHECK(fixture.Format("$d", 160) == "18.00 sec");
	CHECK(fixture.Format("$D", 160) == "18 sec");
	CHECK(fixture.Format("$t0", 160) == "6");
	CHECK(fixture.Format("$o0", 160) == "60");

	// A reference applies the modifiers of the referenced spell
	CHECK(fixture.Format("$160D", 170) == "18 sec");
	CHECK(fixture.Format("$D", 170) == "8 sec");

	// Fire Barrage's total uses the channel's ticks, the projectile has no duration to modify
	CHECK(fixture.Format("$o0", 150) == "57 - 69");

	queried.clear();
	fixture.Spell(330, 0);
	CHECK(fixture.Format("$D", 330) == "0 sec");
	CHECK(queried.empty());
}

TEST_CASE("A spell id prefix reads values from another spell", "[spell_text]")
{
	const SpellFixture fixture;

	CHECK(fixture.Format("each wave deals $152s0 fire damage", 150) == "each wave deals 19 - 23 fire damage");
	CHECK(fixture.Format("$152m0/$152M0", 150) == "19/23");
	CHECK(fixture.Format("$160o0 over $160D in $160t0 ticks every $160I0", 150) == "40 over 12 sec in 4 ticks every 3 sec");
	CHECK(fixture.Format("$150t0 waves", 152) == "3 waves");

	// The prefix redirects one placeholder only
	CHECK(fixture.Format("$152s0 every $i0", 150) == "19 - 23 every 0.50 sec");

	// A reference may point at the spell itself
	CHECK(fixture.Format("$150t0", 150) == "3");

	// The live Fire Barrage description
	CHECK(fixture.Format("Channels $t0 waves of flame at the enemy over $D. Each wave hurls fiery projectiles "
		"that deal $152s0 fire damage, $o0 fire damage in total.", 150) ==
		"Channels 3 waves of flame at the enemy over 1.5 sec. Each wave hurls fiery projectiles "
		"that deal 19 - 23 fire damage, 57 - 69 fire damage in total.");
}

TEST_CASE("A referenced spell scales within its own level range", "[spell_text]")
{
	SpellFixture fixture;
	fixture.spells[152].mutable_effects(0)->set_pointsperlevel(1.0f);

	fixture.context.level = 6;
	CHECK(fixture.Format("$152m0", 150) == "21");

	fixture.context.level = 60;
	CHECK(fixture.Format("$152m0", 150) == "25");
}

TEST_CASE("The effect index is shared between own and referenced placeholders", "[spell_text]")
{
	const SpellFixture fixture;

	// The index sticks to the text, not to the spell: "$s" after "$170s1" reads effect 1
	CHECK(fixture.Format("$170s1 then $s", 170) == "7 - 9 then 7 - 9");
	CHECK(fixture.Format("$170s1 then $s", 152) == "7 - 9 then 0");
}

TEST_CASE("Unresolvable references stay visible", "[spell_text]")
{
	SpellFixture fixture;

	CHECK(fixture.Format("$999s0 damage", 150) == "$999s0 damage");
	CHECK(fixture.Format("$0s0", 150) == "$0s0");

	// More digits than a spell id can hold must not wrap around to an existing spell:
	// 4294967448 is 2^32 + 152
	CHECK(fixture.Format("$4294967448s0", 150) == "$4294967448s0");
	CHECK(fixture.Format("$99999999999999999999999999s0", 150) == "$99999999999999999999999999s0");
	CHECK(fixture.Format("$4294967295s0", 150) == "$4294967295s0");

	fixture.context.findSpell = nullptr;
	CHECK(fixture.Format("$152s0 and $s0", 152) == "$152s0 and 19 - 23");
}

TEST_CASE("Malformed placeholders are written back verbatim", "[spell_text]")
{
	const SpellFixture fixture;

	CHECK(fixture.Format("$", 150) == "$");
	CHECK(fixture.Format("ends with $", 150) == "ends with $");
	CHECK(fixture.Format("ends with $152", 150) == "ends with $152");
	CHECK(fixture.Format("$q and $152q0", 150) == "$q and $152q0");
	CHECK(fixture.Format("$ space", 150) == "$ space");
	CHECK(fixture.Format("$-1", 150) == "$-1");
	CHECK(fixture.Format("100%", 150) == "100%");

	// A '$' before a multibyte character keeps the whole character intact
	CHECK(fixture.Format("$ü", 150) == "$ü");
	CHECK(fixture.Format("$Ж", 150) == "$Ж");
}

TEST_CASE("A doubled dollar writes a literal dollar", "[spell_text]")
{
	const SpellFixture fixture;

	CHECK(fixture.Format("costs $$5", 150) == "costs $5");
	CHECK(fixture.Format("$$", 150) == "$");
	CHECK(fixture.Format("$$$", 150) == "$$");
	CHECK(fixture.Format("$$$t0", 150) == "$3");
	CHECK(fixture.Format("$$s0", 152) == "$s0");
}

TEST_CASE("Extreme effect data saturates instead of overflowing", "[spell_text]")
{
	SpellFixture fixture;
	proto_client::SpellEntry& spell = fixture.Spell(400);

	proto_client::SpellEffect& huge = AddDamage(spell, Int32Max);
	huge.set_basedice(Int32Max);
	huge.set_diesides(Int32Max);
	AddDamage(spell, Int32Min, Int32Min);
	AddDamage(spell, 0).set_pointsperlevel(std::numeric_limits<float>::quiet_NaN());
	AddDamage(spell, 0).set_pointsperlevel(std::numeric_limits<float>::infinity());
	AddDamage(spell, 0).set_pointsperlevel(-std::numeric_limits<float>::infinity());
	AddDamage(spell, 0).set_diceperlevel(std::numeric_limits<float>::quiet_NaN());

	fixture.context.level = 5;
	CHECK(fixture.Format("$s0", 400) == "2147483647");
	CHECK(fixture.Format("$m1 $M1", 400) == "2147483647 2147483647");
	CHECK(fixture.Format("$s2", 400) == "0");
	CHECK(fixture.Format("$s3", 400) == "2147483647");
	CHECK(fixture.Format("$s4", 400) == "2147483647");
	CHECK(fixture.Format("$m5 $M5", 400) == "0 0");
}

TEST_CASE("Extreme levels saturate instead of overflowing", "[spell_text]")
{
	SpellFixture fixture;
	proto_client::SpellEntry& spell = fixture.Spell(410);
	spell.set_spelllevel(Int32Min);
	AddDamage(spell, 1).set_pointsperlevel(1000.0f);

	fixture.context.level = Int32Max;
	CHECK(fixture.Format("$s0", 410) == "2147483647");

	spell.set_spelllevel(Int32Max);
	spell.set_baselevel(Int32Min);
	fixture.context.level = Int32Min;
	CHECK(fixture.Format("$s0", 410) == "2147483647");
}

TEST_CASE("Totals saturate when ticks times points exceed int32", "[spell_text]")
{
	SpellFixture fixture;

	AddPeriodic(fixture.Spell(420, Int32Max), aura_type::PeriodicDamage, 1).set_basepoints(1000);
	CHECK(fixture.Format("$t0 $o0", 420) == "2147483647 2147483647");

	proto_client::SpellEntry& trigger = fixture.Spell(421, Int32Max);
	AddPeriodic(trigger, aura_type::PeriodicTriggerSpell, 1).set_triggerspell(422);
	AddDamage(fixture.Spell(422), Int32Max);
	CHECK(fixture.Format("$o0", 421) == "2147483647");
}

TEST_CASE("GetSpellEffectPoints rejects invalid effect indices", "[spell_text]")
{
	const SpellFixture fixture;
	const proto_client::SpellEntry& spell = fixture.spells.at(170);

	int32 min = -1, max = -1;
	GetSpellEffectPoints(spell, 1, -1, false, min, max);
	CHECK((min == 0 && max == 0));

	min = max = -1;
	GetSpellEffectPoints(spell, 1, 3, true, min, max);
	CHECK((min == 0 && max == 0));

	GetSpellEffectPoints(spell, 1, 1, false, min, max);
	CHECK((min == 7 && max == 9));
}

TEST_CASE("Formatted values replace the first conversion", "[spell_text][spell_text_format]")
{
	CHECK(FormatValue("%.0f seconds", 90.0) == "90 seconds");
	CHECK(FormatValue("%.2f Sekunden", 1.5) == "1.50 Sekunden");
	CHECK(FormatValue("%f", 0.25) == "0.250000");
	CHECK(FormatValue("%5.1f|", 2.0) == "  2.0|");
	CHECK(FormatValue("%-5.1f|", 2.0) == "2.0  |");
	CHECK(FormatValue("%+.0f", 3.0) == "+3");
	CHECK(FormatValue("%.3e", 1500.0) == "1.500e+03");
	CHECK(FormatValue("%g", 0.5) == "0.5");
	CHECK(FormatValue("%lf", 2.5) == "2.500000");
	CHECK(FormatValue("%.1lf min", 2.5) == "2.5 min");
	CHECK(FormatValue("no conversion", 1.0) == "no conversion");
	CHECK(FormatValue("", 1.0).empty());
}

TEST_CASE("Trailing zeros of fixed point values can be trimmed", "[spell_text][spell_text_format]")
{
	std::string out;
	const auto trimmed = [&out](const char* format, const double value)
	{
		out.clear();
		AppendFormattedValue(out, format, value, true);
		return out;
	};

	CHECK(trimmed("%.2f sec", 1.5) == "1.5 sec");
	CHECK(trimmed("%.2f sec", 2.0) == "2 sec");
	CHECK(trimmed("%.2f", 10.0) == "10");
	CHECK(trimmed("%.2f", 100.25) == "100.25");
	CHECK(trimmed("%.0f", 100.0) == "100");
	CHECK(trimmed("%f", 0.0) == "0");
	CHECK(trimmed("%.2f", -0.5) == "-0.5");

	// Left-justified padding ends the output, so nothing is trimmed
	CHECK(trimmed("%-6.2f|", 1.5) == "1.50  |");

	// Only fixed point output is trimmed, never exponents or integers
	CHECK(trimmed("%.2e", 100.0) == "1.00e+02");
	CHECK(trimmed("%d", 100.0) == "100");

	// Without trimming the template decides
	CHECK(FormatValue("%.2f", 2.0) == "2.00");
}

TEST_CASE("Integer conversions round the value", "[spell_text][spell_text_format]")
{
	CHECK(FormatValue("%d sec", 1.6) == "2 sec");
	CHECK(FormatValue("%i", -1.6) == "-2");
	CHECK(FormatValue("%u", 3.0) == "3");
	CHECK(FormatValue("%3d|", 7.0) == "  7|");
	CHECK(FormatValue("%d", 1.0e30) == "9000000000000000000");
	CHECK(FormatValue("%d", std::nan("")) == "0");
}

TEST_CASE("Percent signs and dangerous conversions are written as text", "[spell_text][spell_text_format]")
{
	CHECK(FormatValue("100%% in %.0f s", 5.0) == "100% in 5 s");
	CHECK(FormatValue("%", 1.0) == "%");
	CHECK(FormatValue("50%", 1.0) == "50%");

	// These would read a missing argument, write memory or misinterpret the double in printf
	CHECK(FormatValue("%s", 1.0) == "%s");
	CHECK(FormatValue("%n", 1.0) == "%n");
	CHECK(FormatValue("%p", 1.0) == "%p");
	CHECK(FormatValue("%x", 1.0) == "%x");
	CHECK(FormatValue("%c", 1.0) == "%c");
	CHECK(FormatValue("%*f", 1.0) == "%*f");
	CHECK(FormatValue("%.*f", 1.0) == "%.*f");
	CHECK(FormatValue("%1$f", 1.0) == "%1$f");
	CHECK(FormatValue("%Lf", 1.0) == "%Lf");
	CHECK(FormatValue("%llf", 1.0) == "%llf");
	CHECK(FormatValue("%#d", 1.0) == "%#d");
	CHECK(FormatValue("%s %.0f", 2.0) == "%s 2");
}

TEST_CASE("Only one value is written into a template", "[spell_text][spell_text_format]")
{
	CHECK(FormatValue("%.0f and %.0f", 2.0) == "2 and %.0f");
	CHECK(FormatValue("%.0f %s %d", 2.0) == "2 %s %d");
}

TEST_CASE("Oversized widths and precisions are rejected", "[spell_text][spell_text_format]")
{
	CHECK(FormatValue("%99.0f", 1.0).size() == 99);
	CHECK(FormatValue("%100f", 1.0) == "%100f");
	CHECK(FormatValue("%.100f", 1.0) == "%.100f");
	CHECK(FormatValue("%------f", 1.0) == "%------f");
}

TEST_CASE("Non-finite and huge values never overrun the format buffer", "[spell_text][spell_text_format]")
{
	CHECK_FALSE(FormatValue("%f", std::numeric_limits<double>::infinity()).empty());
	CHECK_FALSE(FormatValue("%f", std::nan("")).empty());

	// 1e308 has 309 integer digits plus 99 decimals: more than the internal buffer
	const std::string huge = FormatValue("%99.99f|", 1.0e308);
	CHECK(huge.size() <= 256);
	CHECK(huge.rfind("1000", 0) == 0);
}

TEST_CASE("The shipped duration formats render as before", "[spell_text][spell_text_format]")
{
	CHECK(FormatValue("%.0f Stunden", 2.0) == "2 Stunden");
	CHECK(FormatValue("%.2f Minuten", 1.5) == "1.50 Minuten");
	CHECK(FormatValue("%.0f secondes", 30.0) == "30 secondes");
	CHECK(FormatValue("%.2f seconds", 0.5) == "0.50 seconds");
}

TEST_CASE("Random text and random data never break the formatter", "[spell_text][spell_text_fuzz]")
{
	SpellFixture fixture;

	// Spells with hostile data next to the regular catalog
	proto_client::SpellEntry& hostile = fixture.Spell(500, Int32Min);
	AddPeriodic(hostile, aura_type::PeriodicTriggerSpell, Int32Min).set_triggerspell(500);
	AddDamage(hostile, Int32Min, Int32Max).set_pointsperlevel(std::numeric_limits<float>::quiet_NaN());
	AddPeriodic(hostile, aura_type::PeriodicDamage, 1).set_basepoints(Int32Max);
	fixture.durationFormats["FORMAT_DURATION_SECONDS"] = "%s%n%*f%.0f%f";

	static constexpr char alphabet[] = "$$$$0123456789sSmMoOtTdDiIqx %\xC3\xBC";
	const uint32 spellIds[] = { 150, 152, 160, 170, 180, 181, 184, 500 };
	const int32 levels[] = { Int32Min, -1, 0, 1, 4, 10, 60, Int32Max };

	std::mt19937 random(0x5eed);
	for (int iteration = 0; iteration < 20000; ++iteration)
	{
		std::string text;
		const size_t length = random() % 48;
		for (size_t i = 0; i < length; ++i)
		{
			text.push_back(alphabet[random() % (sizeof(alphabet) - 1)]);
		}

		fixture.context.level = levels[random() % std::size(levels)];
		const uint32 spellId = spellIds[random() % std::size(spellIds)];

		const std::string first = fixture.Format(text, spellId);
		const std::string second = fixture.Format(text, spellId);
		REQUIRE(first == second);

		if (text.find('$') == std::string::npos)
		{
			REQUIRE(first == text);
		}
	}
}
