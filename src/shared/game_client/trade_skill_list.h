// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/trade_skill.h"
#include "game/spell.h"
#include "client_data/project.h"

#include <map>
#include <vector>

namespace mmo
{
	/// A profession the player has learned, as shown in the profession list.
	struct ProfessionInfo
	{
		/// SkillEntry id of the profession.
		uint32 skillId = 0;
		/// Highest known rank spell of the profession; provides name and icon.
		const proto_client::SpellEntry* unlockSpell = nullptr;
		/// Current skill value (0 if the server has not sent the skill yet).
		uint16 value = 0;
		/// Current skill cap.
		uint16 max = 0;
	};

	/// A craftable recipe the player knows.
	struct RecipeInfo
	{
		/// The recipe spell.
		const proto_client::SpellEntry* spell = nullptr;
		/// Craft category id (SkillEntry category).
		uint32 category = 0;
		/// Colour of the recipe for the current skill value.
		TradeSkillDifficulty difficulty = trade_skill_difficulty::Grey;
		/// Item created by the recipe (0 if none).
		uint32 resultItemId = 0;
		/// Number of items created.
		int32 resultCount = 0;
	};

	/// Professions are the skills referenced by known spells with a TradeSkill effect; per skill the
	/// spell with the highest base points (highest rank) provides name and icon. Skills without packet
	/// state show value 0 / max from the spell. Sorted by skill name.
	std::vector<ProfessionInfo> BuildProfessionList(const std::vector<const proto_client::SpellEntry*>& knownSpells, const proto_client::SkillManager& skills, const std::map<uint32, TradeSkillData>& state);

	/// Known TradeSpell spells whose skill() matches, in category sortorder then name order.
	std::vector<RecipeInfo> BuildRecipeList(uint32 skillId, const std::vector<const proto_client::SpellEntry*>& knownSpells, const proto_client::SkillEntry& skill, uint32 skillValue);

	/// Whether a known spell is listed in the spellbook. Recipes (TradeSpell) are shown in the
	/// profession window instead, and client-side hidden spells never show up.
	inline bool IsSpellBookVisible(const proto_client::SpellEntry& spell)
	{
		constexpr uint32 hiddenMask = static_cast<uint32>(spell_attributes::HiddenClientSide) | static_cast<uint32>(spell_attributes::TradeSpell);
		return (spell.attributes(0) & hiddenMask) == 0;
	}
}
