// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/spells/trade_skill_cast_rules.h"
#include "game_server/objects/game_player_s.h"
#include "test_unit_factory.h"
#include "base/timer_queue.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

using namespace mmo;

namespace
{
	proto::SpellEntry MakeRecipe(const uint32 skill, const uint32 required)
	{
		proto::SpellEntry spell;
		spell.set_id(600);
		spell.add_attributes(spell_attributes::TradeSpell);
		spell.add_attributes(0);
		spell.set_skill(skill);
		spell.set_skillrequired(required);
		spell.set_trivialskilllow(required + 10);
		spell.set_trivialskillhigh(required + 30);
		return spell;
	}
}

TEST_CASE("Recipes require the profession at the required value", "[trade_skill]")
{
	TradeSkillBook book;
	CHECK(CheckTradeSkillRequirement(MakeRecipe(1, 1), book) == spell_cast_result::FailedSkillTooLow);
	book.SetAll({ { 1, 9, 75 } });
	CHECK(CheckTradeSkillRequirement(MakeRecipe(1, 10), book) == spell_cast_result::FailedSkillTooLow);
	book.SetAll({ { 1, 10, 75 } });
	CHECK(CheckTradeSkillRequirement(MakeRecipe(1, 10), book) == spell_cast_result::CastOkay);
}

TEST_CASE("Spells without a skill are unaffected by trade skill rules", "[trade_skill]")
{
	TradeSkillBook book;
	proto::SpellEntry smelt;
	smelt.add_attributes(0);
	smelt.add_attributes(0);
	CHECK(CheckTradeSkillRequirement(smelt, book) == spell_cast_result::CastOkay);
	CHECK_FALSE(ApplyTradeSkillUp(smelt, book, 0.0f));
}

TEST_CASE("A successful craft raises the skill by one", "[trade_skill]")
{
	TradeSkillBook book;
	book.SetAll({ { 1, 1, 75 } });
	CHECK(ApplyTradeSkillUp(MakeRecipe(1, 1), book, 0.99f));
	CHECK(book.Get(1)->value == 2);
}

TEST_CASE("CreateItem spells fail when the bags cannot take the result", "[trade_skill]")
{
	asio::io_service io;
	TimerQueue timers(io);
	proto::Project project;
	auto* item = project.items.add(700);
	item->set_name("Linen Bag");
	item->set_maxstack(1);
	auto player = test::MakeUnit(project, timers);

	proto::SpellEntry spell = MakeRecipe(1, 1);
	auto* effect = spell.add_effects();
	effect->set_type(spell_effects::CreateItem);
	effect->set_itemtype(700);
	effect->set_basepoints(1);
	CHECK(CheckCreateItemSpace(spell, project, player->GetInventory()) == spell_cast_result::CastOkay);

	// Creating items needs a world instance, so instead of filling the backpack ask for one more
	// unstackable item than it has free slots.
	effect->set_basepoints(player_inventory_pack_slots::End - player_inventory_pack_slots::Start + 1);
	CHECK(CheckCreateItemSpace(spell, project, player->GetInventory()) == spell_cast_result::FailedInventoryFull);
}
