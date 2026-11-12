// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// A gossip option of type GossipMenu opens another page of the dialogue. Options on such a page
// name that page when the player picks them, so the server has to accept every page that can be
// reached from the npc's menu - but none that cannot, and none behind a condition the player fails.

#include "catch.hpp"

#include "game_server/gossip_menu_graph.h"

#include <map>
#include <set>

using namespace mmo;

namespace
{
	struct MenuBook
	{
		std::map<uint32, proto::GossipMenuEntry> menus;
		std::set<uint32> metConditions;

		proto::GossipMenuEntry& Add(const uint32 id, const uint32 conditionId = 0)
		{
			proto::GossipMenuEntry& menu = menus[id];
			menu.set_id(id);
			menu.set_name("Menu");
			if (conditionId != 0)
			{
				menu.set_conditionid(conditionId);
			}
			return menu;
		}

		static void AddOption(proto::GossipMenuEntry& menu, const uint32 optionId, const uint32 actionType, const uint32 param = 0, const uint32 conditionId = 0)
		{
			proto::GossipMenuOption* option = menu.add_options();
			option->set_id(optionId);
			option->set_text("Option");
			option->set_action_type(actionType);
			option->set_action_param(param);
			if (conditionId != 0)
			{
				option->set_conditionid(conditionId);
			}
		}

		const proto::GossipMenuEntry* Find(const uint32 rootId, const uint32 menuId) const
		{
			return FindReachableGossipMenu(menus.at(rootId), menuId,
				[this](const uint32 id) -> const proto::GossipMenuEntry*
				{
					const auto it = menus.find(id);
					return it != menus.end() ? &it->second : nullptr;
				},
				[this](const uint32 conditionId) { return metConditions.count(conditionId) != 0; });
		}
	};
}

TEST_CASE("The root gossip menu is reachable from itself", "[gossip]")
{
	MenuBook book;
	book.Add(1);

	const auto* found = book.Find(1, 1);
	REQUIRE(found != nullptr);
	CHECK(found->id() == 1);
}

TEST_CASE("A page opened by a GossipMenu option is reachable", "[gossip]")
{
	MenuBook book;
	MenuBook::AddOption(book.Add(1), 1, gossip_actions::GossipMenu, 2);
	MenuBook::AddOption(book.Add(2), 1, gossip_actions::GossipMenu, 3);
	MenuBook::AddOption(book.Add(3), 1, gossip_actions::Trigger);

	const auto* second = book.Find(1, 2);
	REQUIRE(second != nullptr);
	CHECK(second->id() == 2);

	const auto* third = book.Find(1, 3);
	REQUIRE(third != nullptr);
	CHECK(third->id() == 3);
}

TEST_CASE("A menu no option leads to is not reachable", "[gossip]")
{
	MenuBook book;
	MenuBook::AddOption(book.Add(1), 1, gossip_actions::GossipMenu, 2);
	book.Add(2);
	book.Add(3);

	CHECK(book.Find(1, 3) == nullptr);
	CHECK(book.Find(1, 99) == nullptr);
}

TEST_CASE("Only GossipMenu options lead to another page", "[gossip]")
{
	MenuBook book;

	// A trigger option whose parameter happens to be a menu id opens nothing
	MenuBook::AddOption(book.Add(1), 1, gossip_actions::Trigger, 2);
	book.Add(2);

	CHECK(book.Find(1, 2) == nullptr);
}

TEST_CASE("A page behind an option whose condition fails is not reachable", "[gossip]")
{
	MenuBook book;
	MenuBook::AddOption(book.Add(1), 1, gossip_actions::GossipMenu, 2, 7);
	book.Add(2);

	CHECK(book.Find(1, 2) == nullptr);

	book.metConditions.insert(7);
	CHECK(book.Find(1, 2) != nullptr);
}

TEST_CASE("A sub page with a failing condition of its own is not reachable", "[gossip]")
{
	MenuBook book;
	MenuBook::AddOption(book.Add(1), 1, gossip_actions::GossipMenu, 2);
	book.Add(2, 9);

	CHECK(book.Find(1, 2) == nullptr);

	book.metConditions.insert(9);
	CHECK(book.Find(1, 2) != nullptr);
}

TEST_CASE("Menus that point at each other do not loop the search", "[gossip]")
{
	MenuBook book;
	MenuBook::AddOption(book.Add(1), 1, gossip_actions::GossipMenu, 2);
	MenuBook::AddOption(book.Add(2), 1, gossip_actions::GossipMenu, 1);

	CHECK(book.Find(1, 2) != nullptr);
	CHECK(book.Find(1, 3) == nullptr);
}

TEST_CASE("An option pointing at a missing menu is skipped", "[gossip]")
{
	MenuBook book;
	proto::GossipMenuEntry& root = book.Add(1);
	MenuBook::AddOption(root, 1, gossip_actions::GossipMenu, 50);
	MenuBook::AddOption(root, 2, gossip_actions::GossipMenu, 2);
	book.Add(2);

	CHECK(book.Find(1, 50) == nullptr);
	CHECK(book.Find(1, 2) != nullptr);
}
