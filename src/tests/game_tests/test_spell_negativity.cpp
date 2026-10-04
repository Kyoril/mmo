// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game/spell_negativity.h"
#include "shared/proto_data/spells.pb.h"

#include "catch.hpp"

using namespace mmo;

namespace
{
	constexpr uint64 caster = 1;
	constexpr uint64 victim = 2;

	void AddEffect(proto::SpellEntry& spell, const uint32 type, const uint32 targetA)
	{
		auto* effect = spell.add_effects();
		effect->set_index(spell.effects_size() - 1);
		effect->set_type(type);
		effect->set_targeta(targetA);
	}
}

TEST_CASE("Unflagged DoT on an enemy is a debuff", "[spell_negativity]")
{
	// Holy Fire: direct damage plus a periodic damage aura, no Negative attribute
	proto::SpellEntry spell;
	spell.add_attributes(spell_attributes::Ability);
	AddEffect(spell, spell_effects::SchoolDamage, spell_effect_targets::TargetEnemy);
	AddEffect(spell, spell_effects::ApplyAura, spell_effect_targets::TargetEnemy);

	CHECK(IsNegativeAura(spell, caster, victim));
}

TEST_CASE("Unflagged proc debuff with no attributes is a debuff", "[spell_negativity]")
{
	proto::SpellEntry spell;
	AddEffect(spell, spell_effects::ApplyAura, spell_effect_targets::TargetEnemy);

	CHECK(IsNegativeAura(spell, caster, victim));
}

TEST_CASE("Area debuffs on enemies are debuffs", "[spell_negativity]")
{
	proto::SpellEntry spell;
	AddEffect(spell, spell_effects::ApplyAura, spell_effect_targets::Caster);
	spell.mutable_effects(0)->set_targetb(spell_effect_targets::SourceAreaEnemy);

	CHECK(IsNegativeAura(spell, caster, victim));
}

TEST_CASE("Self and ally auras stay buffs", "[spell_negativity]")
{
	proto::SpellEntry spell;
	AddEffect(spell, spell_effects::ApplyAura, spell_effect_targets::Caster);
	CHECK_FALSE(IsNegativeAura(spell, caster, caster));

	proto::SpellEntry allySpell;
	AddEffect(allySpell, spell_effects::ApplyAura, spell_effect_targets::TargetAlly);
	CHECK_FALSE(IsNegativeAura(allySpell, caster, victim));
}

TEST_CASE("Caster's own aura of a buff-and-debuff spell stays a buff", "[spell_negativity]")
{
	proto::SpellEntry spell;
	AddEffect(spell, spell_effects::ApplyAura, spell_effect_targets::Caster);
	AddEffect(spell, spell_effects::ApplyAura, spell_effect_targets::TargetEnemy);

	CHECK_FALSE(IsNegativeAura(spell, caster, caster));
	CHECK(IsNegativeAura(spell, caster, victim));
}

TEST_CASE("Negative attribute always wins", "[spell_negativity]")
{
	proto::SpellEntry spell;
	spell.add_attributes(spell_attributes::Negative);
	AddEffect(spell, spell_effects::ApplyAura, spell_effect_targets::Caster);

	CHECK(IsNegativeAura(spell, caster, caster));
}
