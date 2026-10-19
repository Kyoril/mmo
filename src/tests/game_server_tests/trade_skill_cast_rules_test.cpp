// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/spells/trade_skill_cast_rules.h"
#include "game_server/objects/game_player_s.h"
#include "test_unit_factory.h"
#include "base/timer_queue.h"
#include "asio/io_service.hpp"

#include "game_server/inventory.h"
#include "game_server/objects/game_item_s.h"

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

namespace mmo
{
	/// Places stacks directly into a player's backpack. Inventory::CreateItems needs a WorldInstance
	/// for item ids, which this suite does not have.
	struct InventoryTestAccess
	{
		static void Fill(Inventory& inventory, const proto::Project& project, const proto::ItemEntry& entry, const uint8 slotCount, const uint8 lastStack)
		{
			for (uint8 i = 0; i < slotCount; ++i)
			{
				auto item = std::make_shared<GameItemS>(project, entry);
				item->Initialize();
				item->Set<uint64>(object_fields::Guid, CreateEntryGUID(1000 + i, entry.id(), GuidType::Item));
				const uint8 stack = (i + 1 == slotCount) ? lastStack : static_cast<uint8>(entry.maxstack());
				(void)item->AddStacks(stack - 1);
				inventory.AddItemToSlot(item, InventorySlot::FromRelative(player_inventory_slots::Bag_0, player_inventory_pack_slots::Start + i).GetAbsolute());
				inventory.m_itemCounter[entry.id()] += stack;
			}
		}
	};
}

TEST_CASE("Stackable crafts need room in an existing stack or a free slot", "[trade_skill]")
{
	asio::io_service io;
	TimerQueue timers(io);
	proto::Project project;
	auto* item = project.items.add(701);
	item->set_name("Bolt of Linen Cloth");
	item->set_maxstack(20);
	auto player = test::MakeUnit(project, timers);

	proto::SpellEntry spell = MakeRecipe(1, 1);
	auto* effect = spell.add_effects();
	effect->set_type(spell_effects::CreateItem);
	effect->set_itemtype(701);
	effect->set_basepoints(1);

	constexpr uint8 slots = player_inventory_pack_slots::End - player_inventory_pack_slots::Start;

	// Every slot holds a full stack: nothing fits.
	InventoryTestAccess::Fill(player->GetInventory(), project, *item, slots, 20);
	CHECK(CheckCreateItemSpace(spell, project, player->GetInventory()) == spell_cast_result::FailedInventoryFull);
}

TEST_CASE("Stackable crafts fit into a partial stack with no free slot", "[trade_skill]")
{
	asio::io_service io;
	TimerQueue timers(io);
	proto::Project project;
	auto* item = project.items.add(701);
	item->set_name("Bolt of Linen Cloth");
	item->set_maxstack(20);
	auto player = test::MakeUnit(project, timers);

	proto::SpellEntry spell = MakeRecipe(1, 1);
	auto* effect = spell.add_effects();
	effect->set_type(spell_effects::CreateItem);
	effect->set_itemtype(701);
	effect->set_basepoints(1);

	constexpr uint8 slots = player_inventory_pack_slots::End - player_inventory_pack_slots::Start;
	InventoryTestAccess::Fill(player->GetInventory(), project, *item, slots, 19);
	CHECK(CheckCreateItemSpace(spell, project, player->GetInventory()) == spell_cast_result::CastOkay);

	effect->set_basepoints(2);
	CHECK(CheckCreateItemSpace(spell, project, player->GetInventory()) == spell_cast_result::FailedInventoryFull);
}
