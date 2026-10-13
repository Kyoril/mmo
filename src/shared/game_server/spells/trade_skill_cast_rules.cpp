// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "trade_skill_cast_rules.h"

#include "game_server/inventory.h"
#include "proto_data/project.h"

namespace mmo
{
	SpellCastResult CheckTradeSkillRequirement(const proto::SpellEntry& spell, const TradeSkillBook& skills)
	{
		if (spell.skill() == 0)
		{
			return spell_cast_result::CastOkay;
		}

		const TradeSkillData* data = skills.Get(spell.skill());
		if (!data || data->value < spell.skillrequired())
		{
			return spell_cast_result::FailedSkillTooLow;
		}

		return spell_cast_result::CastOkay;
	}

	SpellCastResult CheckCreateItemSpace(const proto::SpellEntry& spell, const proto::Project& project, const Inventory& inventory)
	{
		for (const auto& effect : spell.effects())
		{
			if (effect.type() != spell_effects::CreateItem || effect.basepoints() <= 0)
			{
				continue;
			}

			const proto::ItemEntry* item = project.items.getById(effect.itemtype());
			if (!item)
			{
				continue;
			}

			if (inventory.CanStoreItems(*item, static_cast<uint16>(effect.basepoints())) != inventory_change_failure::Okay)
			{
				return spell_cast_result::FailedInventoryFull;
			}
		}

		return spell_cast_result::CastOkay;
	}

	bool ApplyTradeSkillUp(const proto::SpellEntry& spell, TradeSkillBook& skills, const float roll)
	{
		if (spell.skill() == 0)
		{
			return false;
		}

		return skills.TrySkillUp(spell.skill(), TradeSkillThresholdsOf(spell), roll);
	}
}
