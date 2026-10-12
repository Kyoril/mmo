// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_client/trade_skill_list.h"
#include "game/spell.h"

#include "catch.hpp"

using namespace mmo;

namespace
{
	proto_client::SpellEntry MakeUnlock(const uint32 id, const uint32 skillId, const int32 cap, const std::string& name)
	{
		proto_client::SpellEntry spell;
		spell.set_id(id);
		spell.set_name(name);
		spell.add_attributes(spell_attributes::Passive);
		spell.add_attributes(0);
		auto* effect = spell.add_effects();
		effect->set_type(spell_effects::TradeSkill);
		effect->set_miscvaluea(skillId);
		effect->set_basepoints(cap);
		return spell;
	}

	proto_client::SpellEntry MakeRecipe(const uint32 id, const uint32 skillId, const uint32 category, const uint32 item)
	{
		proto_client::SpellEntry spell;
		spell.set_id(id);
		spell.set_name("Recipe " + std::to_string(id));
		spell.add_attributes(spell_attributes::TradeSpell);
		spell.add_attributes(0);
		spell.set_skill(skillId);
		spell.set_skillrequired(1);
		spell.set_trivialskilllow(20);
		spell.set_trivialskillhigh(40);
		spell.set_craftcategory(category);
		auto* effect = spell.add_effects();
		effect->set_type(spell_effects::CreateItem);
		effect->set_itemtype(item);
		effect->set_basepoints(1);
		return spell;
	}
}

TEST_CASE("Professions come from known TradeSkill spells, highest rank wins", "[trade_skill]")
{
	proto_client::SkillManager skills;
	auto* tailoring = skills.add(1);
	tailoring->set_name("Tailoring");
	tailoring->set_category(1);
	tailoring->set_cost(0);

	const auto apprentice = MakeUnlock(10, 1, 75, "Tailoring (Apprentice)");
	const auto journeyman = MakeUnlock(11, 1, 150, "Tailoring (Journeyman)");
	const auto recipe = MakeRecipe(20, 1, 2, 700);
	const std::vector<const proto_client::SpellEntry*> known{ &apprentice, &recipe, &journeyman };

	std::map<uint32, TradeSkillData> state{ { 1, { 1, 33, 150 } } };
	const auto professions = BuildProfessionList(known, skills, state);
	REQUIRE(professions.size() == 1);
	CHECK(professions[0].unlockSpell->id() == 11);
	CHECK(professions[0].value == 33);
	CHECK(professions[0].max == 150);
}

TEST_CASE("Recipes are filtered by skill, sorted by category and coloured", "[trade_skill]")
{
	proto_client::SkillEntry skill;
	skill.set_id(1);
	skill.set_name("Tailoring");
	skill.set_category(1);
	skill.set_cost(0);
	auto* bags = skill.add_categories();
	bags->set_id(2);
	bags->set_name("Bags");
	bags->set_sortorder(20);
	auto* materials = skill.add_categories();
	materials->set_id(1);
	materials->set_name("Materials");
	materials->set_sortorder(10);

	const auto bag = MakeRecipe(21, 1, 2, 701);
	const auto bolt = MakeRecipe(20, 1, 1, 700);
	const auto other = MakeRecipe(30, 2, 1, 800);
	const std::vector<const proto_client::SpellEntry*> known{ &bag, &other, &bolt };

	const auto recipes = BuildRecipeList(1, known, skill, 25);
	REQUIRE(recipes.size() == 2);
	CHECK(recipes[0].spell->id() == 20);
	CHECK(recipes[1].spell->id() == 21);
	CHECK(recipes[0].difficulty == trade_skill_difficulty::Yellow);
	CHECK(recipes[0].resultItemId == 700);
	CHECK(recipes[0].resultCount == 1);
}

TEST_CASE("Unknown professions without packet state are still listed", "[trade_skill]")
{
	proto_client::SkillManager skills;
	auto* tailoring = skills.add(1);
	tailoring->set_name("Tailoring");
	tailoring->set_category(1);
	tailoring->set_cost(0);
	const auto apprentice = MakeUnlock(10, 1, 75, "Tailoring (Apprentice)");
	const std::vector<const proto_client::SpellEntry*> known{ &apprentice };
	const auto professions = BuildProfessionList(known, skills, {});
	REQUIRE(professions.size() == 1);
	CHECK(professions[0].value == 0);
	CHECK(professions[0].max == 75);
}
