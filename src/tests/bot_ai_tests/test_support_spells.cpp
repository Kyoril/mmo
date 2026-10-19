// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "bot_ai/bot_ai_registry.h"
#include "bot_ai/bot_rotation.h"
#include "bot_ai/strategies/economy_strategy.h"
#include "bot_ai/strategies/grind_strategy.h"
#include "bot_ai/strategies/party_strategy.h"
#include "bot_ai/strategies/progression_strategy.h"
#include "bot_ai/strategies/support_strategy.h"

#include "game/spell.h"
#include "game/spell_target_map.h"
#include "proto_data/project.h"

#include <sstream>

using namespace mmo;

namespace
{
	/// A spell whose effects all aim at the given target type. The data leaves the positive
	/// flag unset, so who an effect aims at is what tells a buff from a debuff.
	proto::SpellEntry makeSpell(const uint32 target, const std::vector<uint32>& effects, const int32 durationMs = -1, const uint64 cooldownMs = 0)
	{
		proto::SpellEntry spell;
		spell.set_id(1);
		spell.set_name("test");
		spell.set_duration(durationMs);
		spell.set_cooldown(cooldownMs);
		for (const uint32 type : effects)
		{
			proto::SpellEffect* effect = spell.add_effects();
			effect->set_index(spell.effects_size() - 1);
			effect->set_type(type);
			effect->set_targeta(target);
		}
		return spell;
	}

	constexpr uint32 Self = spell_effect_targets::Caster;
	constexpr uint32 Ally = spell_effect_targets::TargetAlly;
	constexpr uint32 Enemy = spell_effect_targets::TargetEnemy;
}

TEST_CASE("Heals heal a friend and do no damage", "[bot_ai][support]")
{
	// Healing Light: a heal effect at a friendly target.
	CHECK(IsHealBotSpell(makeSpell(Ally, { spell_effects::Heal })));
	CHECK(IsHealBotSpell(makeSpell(Self, { spell_effects::HealPct })));
	CHECK_FALSE(IsHealBotSpell(makeSpell(Enemy, { spell_effects::Heal })));
	CHECK_FALSE(IsHealBotSpell(makeSpell(Ally, { spell_effects::Heal, spell_effects::SchoolDamage })));
	CHECK_FALSE(IsHealBotSpell(makeSpell(Ally, { spell_effects::ApplyAura })));
}

TEST_CASE("Self-buffs are long auras on a friend that do nothing else", "[bot_ai][support]")
{
	// Frost Armor, Arcane Intellect: half-hour auras on the caster or a friend.
	CHECK(IsSelfBuffBotSpell(makeSpell(Self, { spell_effects::ApplyAura }, 30 * 60 * 1000)));
	CHECK(IsSelfBuffBotSpell(makeSpell(Ally, { spell_effects::ApplyAura }, -1)));

	// Rend and Corrupting Touch: the same aura effect, aimed at an enemy - a debuff.
	CHECK_FALSE(IsSelfBuffBotSpell(makeSpell(Enemy, { spell_effects::ApplyAura }, 30 * 60 * 1000)));

	// Too short to keep up, on a real cooldown, or doing more than buffing.
	CHECK_FALSE(IsSelfBuffBotSpell(makeSpell(Self, { spell_effects::ApplyAura }, 10 * 1000)));
	CHECK_FALSE(IsSelfBuffBotSpell(makeSpell(Self, { spell_effects::ApplyAura }, -1, 120 * 1000)));
	CHECK_FALSE(IsSelfBuffBotSpell(makeSpell(Self, { spell_effects::ApplyAura, spell_effects::ChangeClass }, -1)));
	CHECK_FALSE(IsSelfBuffBotSpell(makeSpell(Self, { spell_effects::Summon }, -1)));
}

TEST_CASE("Every bot strategy refers only to registered definitions", "[bot_ai][support]")
{
	BotAiRegistry registry;
	RegisterGrindStrategies(registry);
	RegisterPartyStrategies(registry);
	RegisterEconomyStrategies(registry);
	RegisterProgressionStrategies(registry);
	RegisterSupportStrategies(registry);

	for (const char* name : { "economy_world", "progression_world", "support_world", "support_combat" })
	{
		CHECK(registry.FindStrategy(name) != nullptr);
	}

	std::ostringstream dangling;
	for (const std::string& name : registry.FindDanglingReferences())
	{
		dangling << name << ' ';
	}

	INFO("dangling: " << dangling.str());
	CHECK(registry.FindDanglingReferences().empty());
}
