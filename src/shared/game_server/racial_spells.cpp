// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "racial_spells.h"

#include "game/spell.h"

#include <algorithm>

namespace mmo
{
	std::vector<uint32> AppendRacialSpells(const proto::RaceEntry& race, const proto::SpellManager& spells, std::vector<uint32>& spellIds)
	{
		std::vector<uint32> added;

		for (const uint32 spellId : race.racialspells())
		{
			if (spells.getById(spellId) == nullptr)
			{
				continue;
			}

			if (std::find(spellIds.begin(), spellIds.end(), spellId) != spellIds.end())
			{
				continue;
			}

			spellIds.push_back(spellId);
			added.push_back(spellId);
		}

		return added;
	}

	bool IsActionBarAbility(const proto::SpellEntry& spell)
	{
		if (spell.attributes_size() == 0)
		{
			return false;
		}

		const uint32 attributes = spell.attributes(0);
		return (attributes & spell_attributes::Passive) == 0 &&
			(attributes & spell_attributes::HiddenClientSide) == 0 &&
			(attributes & spell_attributes::Ability) != 0;
	}
}
