// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_spell_info.h"

#include "game/spell_offense.h"

namespace mmo
{
	BotSpellInfo DescribeBotSpell(const proto_client::SpellEntry& spell, const proto_client::RangeManager& ranges)
	{
		BotSpellInfo info;
		info.offensive = IsOffensiveSpell(spell);
		info.castTime = spell.casttime();
		info.cost = spell.cost();
		info.powerType = spell.powertype();
		info.score = EstimateSpellDamagePerCastSecond(spell);

		if (spell.rangetype() != 0)
		{
			if (const proto_client::RangeType* range = ranges.getById(spell.rangetype()))
			{
				info.maxRange = range->range();
			}
		}

		return info;
	}
}
