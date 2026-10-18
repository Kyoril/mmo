// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "spell.h"

#include <algorithm>

namespace mmo
{
	/// @brief Whether an effect hurts whatever it lands on. A spell carrying one of these is a
	///        spell a bot can open a fight with.
	constexpr bool IsDamagingSpellEffect(const uint32 type)
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

	/// @brief Whether this spell is something a bot would cast at an enemy: it damages, it is
	///        not passive, and it is not a beneficial effect aimed at a friend.
	/// @tparam TSpellEntry Either proto::SpellEntry (headless bots) or proto_client::SpellEntry
	///         (the in-client bot), so both bots judge spells the same way.
	template<typename TSpellEntry>
	bool IsOffensiveSpell(const TSpellEntry& spell)
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

		for (const auto& effect : spell.effects())
		{
			if (IsDamagingSpellEffect(effect.type()))
			{
				return true;
			}
		}

		return false;
	}

	/// @brief Rough expected damage per second of cast time, for ordering a rotation only. The
	///        estimate does not have to match what the server rolls.
	/// @tparam TSpellEntry Either proto::SpellEntry or proto_client::SpellEntry.
	template<typename TSpellEntry>
	float EstimateSpellDamagePerCastSecond(const TSpellEntry& spell)
	{
		float total = 0.0f;

		for (const auto& effect : spell.effects())
		{
			if (!IsDamagingSpellEffect(effect.type()))
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
}
