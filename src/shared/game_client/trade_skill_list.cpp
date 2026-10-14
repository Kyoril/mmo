// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "trade_skill_list.h"

#include "game/spell.h"

#include <algorithm>
#include <limits>

namespace mmo
{
	std::vector<ProfessionInfo> BuildProfessionList(const std::vector<const proto_client::SpellEntry*>& knownSpells, const proto_client::SkillManager& skills, const std::map<uint32, TradeSkillData>& state)
	{
		struct Candidate
		{
			const proto_client::SpellEntry* spell = nullptr;
			int32 cap = 0;
		};

		std::map<uint32, Candidate> best;
		for (const auto* spell : knownSpells)
		{
			if (spell == nullptr)
			{
				continue;
			}

			for (const auto& effect : spell->effects())
			{
				if (effect.type() != spell_effects::TradeSkill)
				{
					continue;
				}

				const uint32 skillId = static_cast<uint32>(effect.miscvaluea());
				auto it = best.find(skillId);
				if (it == best.end() || effect.basepoints() > it->second.cap)
				{
					best[skillId] = { spell, effect.basepoints() };
				}
			}
		}

		std::vector<std::pair<std::string, ProfessionInfo>> sorted;
		for (const auto& [skillId, candidate] : best)
		{
			const auto* skill = skills.getById(skillId);
			if (skill == nullptr)
			{
				continue;
			}

			ProfessionInfo info;
			info.skillId = skillId;
			info.unlockSpell = candidate.spell;
			if (const auto stateIt = state.find(skillId); stateIt != state.end())
			{
				info.value = stateIt->second.value;
				info.max = stateIt->second.max;
			}
			else
			{
				info.value = 0;
				info.max = static_cast<uint16>(std::clamp<int32>(candidate.cap, 0, std::numeric_limits<uint16>::max()));
			}

			sorted.emplace_back(skill->name(), info);
		}

		std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

		std::vector<ProfessionInfo> result;
		result.reserve(sorted.size());
		for (const auto& entry : sorted)
		{
			result.push_back(entry.second);
		}
		return result;
	}

	bool GetSpellCreatedItem(const proto_client::SpellEntry& spell, uint32& outItemId, int32& outCount)
	{
		outItemId = 0;
		outCount = 0;

		for (const auto& effect : spell.effects())
		{
			if (effect.type() == spell_effects::CreateItem)
			{
				outItemId = effect.itemtype();
				outCount = effect.basepoints();
				return true;
			}
		}

		return false;
	}

	std::vector<RecipeInfo> BuildRecipeList(const uint32 skillId, const std::vector<const proto_client::SpellEntry*>& knownSpells, const proto_client::SkillEntry& skill, const uint32 skillValue)
	{
		std::map<uint32, uint32> sortOrder;
		for (const auto& category : skill.categories())
		{
			sortOrder[category.id()] = category.sortorder();
		}

		const auto orderOf = [&sortOrder](const uint32 category)
		{
			const auto it = sortOrder.find(category);
			return it != sortOrder.end() ? it->second : std::numeric_limits<uint32>::max();
		};

		std::vector<RecipeInfo> result;
		for (const auto* spell : knownSpells)
		{
			if (spell == nullptr || spell->skill() != skillId || (spell->attributes(0) & static_cast<uint32>(spell_attributes::TradeSpell)) == 0)
			{
				continue;
			}

			RecipeInfo info;
			info.spell = spell;
			info.category = spell->craftcategory();
			info.difficulty = GetTradeSkillDifficulty(skillValue, TradeSkillThresholdsOf(*spell));
			for (const auto& effect : spell->effects())
			{
				if (effect.type() == spell_effects::CreateItem)
				{
					info.resultItemId = effect.itemtype();
					info.resultCount = effect.basepoints();
					break;
				}
			}

			result.push_back(info);
		}

		std::stable_sort(result.begin(), result.end(), [&orderOf](const RecipeInfo& a, const RecipeInfo& b)
		{
			const uint32 oa = orderOf(a.category);
			const uint32 ob = orderOf(b.category);
			if (oa != ob)
			{
				return oa < ob;
			}
			return a.spell->name() < b.spell->name();
		});
		return result;
	}
}
