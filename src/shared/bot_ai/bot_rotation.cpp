// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_rotation.h"

#include "bot_core/bot_context.h"

#include "game/spell_offense.h"
#include "log/default_log_levels.h"
#include "proto_data/project.h"

#include <algorithm>

namespace mmo
{
	bool IsOffensiveBotSpell(const proto::SpellEntry& spell)
	{
		return IsOffensiveSpell(spell);
	}

	float EstimateBotSpellDamage(const proto::SpellEntry& spell)
	{
		return EstimateSpellDamagePerCastSecond(spell);
	}

	std::size_t BotRotation::Rebuild(const proto::Project& project, const BotContext& world)
	{
		m_spells.clear();

		const std::vector<uint32> known = world.GetKnownSpellIds();
		m_sourceSpellCount = known.size();

		for (const uint32 spellId : known)
		{
			const proto::SpellEntry* spell = project.spells.getById(spellId);
			if (!spell || !IsOffensiveBotSpell(*spell))
			{
				continue;
			}

			BotRotationSpell entry;
			entry.spellId = spellId;
			entry.cost = spell->cost();
			entry.costPercent = spell->costpct();
			entry.castTimeMs = spell->casttime();
			entry.minRange = spell->minrange();
			entry.estimatedDamage = EstimateBotSpellDamage(*spell);

			// Range lives behind an indirection: the spell names a range type, and the range
			// table holds the distance. This is the same lookup the server does when it decides
			// whether to reject the cast.
			if (spell->has_rangetype())
			{
				if (const proto::RangeType* rangeType = project.ranges.getById(spell->rangetype()))
				{
					entry.maxRange = rangeType->range();
				}
			}

			m_spells.push_back(entry);
		}

		// Best first, so selection is a scan that stops at the first usable spell.
		std::sort(m_spells.begin(), m_spells.end(), [](const BotRotationSpell& a, const BotRotationSpell& b)
			{
				return a.estimatedDamage > b.estimatedDamage;
			});

		return m_spells.size();
	}

	const BotRotationSpell* BotRotation::SelectSpellEntry(const BotContext& world, const float targetDistance) const
	{
		const uint32 power = world.GetSelfPower();
		const uint32 maxPower = world.GetSelfMaxPower();

		for (const BotRotationSpell& spell : m_spells)
		{
			if (world.IsSpellOnCooldown(spell.spellId))
			{
				continue;
			}

			if (spell.maxRange > 0.0f && targetDistance > spell.maxRange)
			{
				continue;
			}

			if (spell.minRange > 0.0f && targetDistance < spell.minRange)
			{
				continue;
			}

			uint32 required = spell.cost;
			if (spell.costPercent > 0 && maxPower > 0)
			{
				required += maxPower * spell.costPercent / 100;
			}

			if (required > power)
			{
				continue;
			}

			return &spell;
		}

		return nullptr;
	}

	uint32 BotRotation::SelectSpell(const BotContext& world, const float targetDistance) const
	{
		const BotRotationSpell* spell = SelectSpellEntry(world, targetDistance);
		return spell ? spell->spellId : 0;
	}
}
