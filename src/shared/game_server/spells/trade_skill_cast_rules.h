// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game/spell.h"
#include "game_server/trade_skill_book.h"
#include "shared/proto_data/spells.pb.h"

namespace mmo
{
	class Inventory;

	namespace proto
	{
		class Project;
	}

	/// Checks whether a player knows the profession a recipe needs, at a high enough value.
	/// @param spell The spell being cast.
	/// @param skills The caster's trade skill book.
	/// @return FailedSkillTooLow if the recipe needs a profession the player lacks or has too low;
	///	CastOkay otherwise (also for spells with skill() == 0).
	SpellCastResult CheckTradeSkillRequirement(const proto::SpellEntry& spell, const TradeSkillBook& skills);

	/// Checks whether the items a spell creates for its caster fit into the caster's bags.
	/// Assumes the created amount of a CreateItem effect is its basepoints (recipes have no base dice).
	/// @param spell The spell being cast.
	/// @param project The project holding the item templates.
	/// @param inventory The caster's inventory.
	/// @return FailedInventoryFull if any CreateItem effect cannot be stored; CastOkay otherwise.
	SpellCastResult CheckCreateItemSpace(const proto::SpellEntry& spell, const proto::Project& project, const Inventory& inventory);

	/// Rolls the skill-up for a successful cast.
	/// @param spell The spell that was cast.
	/// @param skills The caster's trade skill book.
	/// @param roll A random value in [0, 1).
	/// @return True if the skill value rose. Always false for spells with skill() == 0.
	bool ApplyTradeSkillUp(const proto::SpellEntry& spell, TradeSkillBook& skills, float roll);
}
