// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/gossip.h"
#include "shared/proto_data/gossip_menus.pb.h"

#include <set>
#include <vector>

namespace mmo
{
	/// Upper bound of gossip menus looked at when following sub menus. A dialogue of an npc is a
	/// handful of pages; the bound only keeps malformed data (a huge or endless menu graph) cheap.
	constexpr size_t MaxReachableGossipMenus = 32;

	/// Finds a gossip menu by id among the pages a player can reach from an npc's root menu.
	///
	/// A gossip option of type GossipMenu opens another menu, which makes a dialogue a small graph
	/// of pages. An option the player picks on such a sub page names that page, not the root, so
	/// the server has to accept every page that can be reached from the root through options
	/// whose conditions the player meets - and nothing else, or a client could execute options of
	/// menus the npc never offered it.
	///
	/// @param root The menu the npc currently offers the player.
	/// @param menuId Id of the menu the player claims to act on.
	/// @param lookup Callable `const proto::GossipMenuEntry*(uint32 menuId)`, nullptr if unknown.
	/// @param meetsCondition Callable `bool(uint32 conditionId)` for the player in question.
	/// @returns The menu, or nullptr if it cannot be reached from the root.
	template<typename MenuLookup, typename ConditionCheck>
	const proto::GossipMenuEntry* FindReachableGossipMenu(
		const proto::GossipMenuEntry& root,
		const uint32 menuId,
		MenuLookup&& lookup,
		ConditionCheck&& meetsCondition)
	{
		std::set<uint32> visited;
		std::vector<const proto::GossipMenuEntry*> open;
		open.push_back(&root);
		visited.insert(root.id());

		while (!open.empty())
		{
			const proto::GossipMenuEntry* menu = open.back();
			open.pop_back();

			if (menu->id() == menuId)
			{
				return menu;
			}

			for (const proto::GossipMenuOption& option : menu->options())
			{
				if (option.action_type() != gossip_actions::GossipMenu)
				{
					continue;
				}

				if (option.conditionid() != 0 && !meetsCondition(option.conditionid()))
				{
					continue;
				}

				if (visited.size() >= MaxReachableGossipMenus || !visited.insert(option.action_param()).second)
				{
					continue;
				}

				const proto::GossipMenuEntry* subMenu = lookup(option.action_param());
				if (subMenu == nullptr)
				{
					continue;
				}

				// A sub menu may carry a condition of its own
				if (subMenu->conditionid() != 0 && !meetsCondition(subMenu->conditionid()))
				{
					continue;
				}

				open.push_back(subMenu);
			}
		}

		return nullptr;
	}
}
