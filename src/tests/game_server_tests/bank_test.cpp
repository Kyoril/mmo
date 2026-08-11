// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game/bank.h"
#include "game/item.h"
#include "game/object_type_id.h"
#include "game_server/inventory.h"
#include "game_server/inventory_types.h"
#include "game_server/objects/game_player_s.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

using namespace mmo;

namespace
{
	/// Build a minimal GamePlayerS with a class entry so SetLevel() works.
	std::shared_ptr<GamePlayerS> MakeBankTestPlayer(proto::Project& project, TimerQueue& timers)
	{
		auto* cls = project.classes.getById(1);
		if (!cls)
		{
			cls = project.classes.add(1);
			if (cls)
			{
				cls->set_powertype(proto::ClassEntry_PowerType_MANA);
				for (uint32 i = 0; i < 2; ++i)
				{
					auto* lbv = cls->add_levelbasevalues();
					lbv->set_health(100);
					lbv->set_mana(100);
					lbv->set_stamina(10);
					lbv->set_strength(10);
					lbv->set_agility(10);
					lbv->set_intellect(10);
					lbv->set_spirit(10);
				}
			}
		}

		auto player = std::make_shared<GamePlayerS>(project, timers);
		player->Initialize();
		if (cls)
		{
			player->SetClass(*cls);
		}
		player->SetLevel(1);
		return player;
	}
}

TEST_CASE("Inventory - IsValidSlot handles bank slots", "[bank]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	const auto player = MakeBankTestPlayer(project, timers);
	const Inventory& inventory = player->GetInventory();

	proto::ItemEntry weapon;
	weapon.set_inventorytype(inventory_type::Weapon);

	proto::ItemEntry bag;
	bag.set_inventorytype(inventory_type::Bag);

	SECTION("Bank item slots accept any item")
	{
		const uint16 bankSlot = InventorySlot::FromRelative(player_inventory_slots::Bag_0, player_bank_item_slots::Start).GetAbsolute();
		REQUIRE(inventory.IsValidSlot(bankSlot, weapon) == inventory_change_failure::Okay);
		REQUIRE(inventory.IsValidSlot(bankSlot, bag) == inventory_change_failure::Okay);
	}

	SECTION("Bank bag slots reject non-bags")
	{
		player->Set<uint32>(object_fields::BankBagSlotCount, 7);

		const uint16 bankBagSlot = InventorySlot::FromRelative(player_inventory_slots::Bag_0, player_bank_bag_slots::Start).GetAbsolute();
		REQUIRE(inventory.IsValidSlot(bankBagSlot, weapon) == inventory_change_failure::NotABag);
	}

	SECTION("Bank bag slots must be purchased")
	{
		const uint16 bankBagSlot = InventorySlot::FromRelative(player_inventory_slots::Bag_0, player_bank_bag_slots::Start).GetAbsolute();
		REQUIRE(inventory.IsValidSlot(bankBagSlot, bag) == inventory_change_failure::MustPurchaseThatBagSlot);

		player->Set<uint32>(object_fields::BankBagSlotCount, 1);
		REQUIRE(inventory.IsValidSlot(bankBagSlot, bag) == inventory_change_failure::Okay);

		const uint16 secondBankBagSlot = InventorySlot::FromRelative(player_inventory_slots::Bag_0, player_bank_bag_slots::Start + 1).GetAbsolute();
		REQUIRE(inventory.IsValidSlot(secondBankBagSlot, bag) == inventory_change_failure::MustPurchaseThatBagSlot);
	}
}

TEST_CASE("Bank - GetBankBagSlotPrice", "[bank]")
{
	SECTION("Prices increase for the first slots")
	{
		REQUIRE(GetBankBagSlotPrice(0) > 0);
		REQUIRE(GetBankBagSlotPrice(1) > GetBankBagSlotPrice(0));
		REQUIRE(GetBankBagSlotPrice(2) > GetBankBagSlotPrice(1));
	}

	SECTION("Every purchasable slot has a price")
	{
		for (uint8 i = 0; i < player_bank_bag_slots::Count_; ++i)
		{
			REQUIRE(GetBankBagSlotPrice(i) > 0);
		}
	}

	SECTION("Out of range slot indices have no price")
	{
		REQUIRE(GetBankBagSlotPrice(player_bank_bag_slots::Count_) == 0);
		REQUIRE(GetBankBagSlotPrice(0xFF) == 0);
	}
}
