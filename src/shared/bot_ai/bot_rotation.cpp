// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_rotation.h"

#include "bot_core/bot_context.h"

#include "game/spell.h"
#include "log/default_log_levels.h"
#include "proto_data/project.h"

#include <algorithm>

namespace mmo
{
	namespace
	{
		/// Effects that hurt whatever they land on. A spell carrying one of these is a spell a
		/// bot can open a fight with.
		[[nodiscard]] bool isDamagingEffect(const uint32 type)
		{
			switch (type)
			{
			case spell_effects::SchoolDamage:
			case spell_effects::WeaponDamage:
			case spell_effects::WeaponDamageNoSchool:
			case spell_effects::WeaponPercentDamage:
			case spell_effects::HealthLeech:
			case spell_effects::EnvironmentalDamage:
				return true;
			default:
				return false;
			}
		}
	}

	bool IsOffensiveBotSpell(const proto::SpellEntry& spell)
	{
		if (spell.attributes_size() > 0 && (spell.attributes(0) & spell_attributes::Passive) != 0)
		{
			return false;
		}

		// A spell flagged positive is meant for a friend. Some of them still carry a damaging
		// effect - a heal-over-time that ticks damage on undead, say - and casting those at a
		// creature just wastes power and a global cooldown.
		if (spell.positive() != 0)
		{
			return false;
		}

		for (int i = 0; i < spell.effects_size(); ++i)
		{
			if (isDamagingEffect(spell.effects(i).type()))
			{
				return true;
			}
		}

		return false;
	}

	float EstimateBotSpellDamage(const proto::SpellEntry& spell)
	{
		float total = 0.0f;

		for (int i = 0; i < spell.effects_size(); ++i)
		{
			const proto::SpellEffect& effect = spell.effects(i);
			if (!isDamagingEffect(effect.type()))
			{
				continue;
			}

			// Mean of the roll rather than its maximum: two spells are being compared, and using
			// the maximum would rank a wide low-average roll above a narrow high-average one.
			const float dice = effect.diesides() > 0
				? (static_cast<float>(effect.diesides()) + 1.0f) * 0.5f
				: 0.0f;

			total += static_cast<float>(effect.basepoints()) + dice;
		}

		// A cast time is a cost. Ranking by damage alone would put a three-second nuke above an
		// instant that does two thirds of the damage, which is the wrong way round for a bot
		// that is being hit while it casts.
		const float castSeconds = std::max(1.0f, static_cast<float>(spell.casttime()) / 1000.0f);
		return total / castSeconds;
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
