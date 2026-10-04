// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "spell.h"
#include "spell_target_map.h"

namespace mmo
{
	/// @brief Returns true if the given effect target type selects hostile units.
	inline bool IsHostileEffectTarget(const uint32 target)
	{
		switch (target)
		{
		case spell_effect_targets::NearbyEnemy:
		case spell_effect_targets::TargetEnemy:
		case spell_effect_targets::SourceAreaEnemy:
		case spell_effect_targets::TargetAreaEnemy:
		case spell_effect_targets::ConeEnemy:
		case spell_effect_targets::TargetSecondaryEnemy:
			return true;
		default:
			return false;
		}
	}

	/// @brief Returns true if the spell places an aura on hostile units, which makes that aura
	///        a debuff even when the spell lacks the spell_attributes::Negative flag.
	/// @tparam TSpellEntry Either proto::SpellEntry (server / editor) or proto_client::SpellEntry.
	template<typename TSpellEntry>
	bool SpellAppliesHostileAura(const TSpellEntry& spell)
	{
		for (const auto& effect : spell.effects())
		{
			if (effect.type() != spell_effects::ApplyAura &&
				effect.type() != spell_effects::PersistentAreaAura)
			{
				continue;
			}

			if (IsHostileEffectTarget(effect.targeta()) || IsHostileEffectTarget(effect.targetb()))
			{
				return true;
			}
		}

		return false;
	}

	/// @brief Returns true if the spell is explicitly flagged as spell_attributes::Negative.
	template<typename TSpellEntry>
	bool HasNegativeAttribute(const TSpellEntry& spell)
	{
		return spell.attributes_size() > 0 && (spell.attributes(0) & spell_attributes::Negative) != 0;
	}

	/// @brief Decides whether an aura of the given spell is a debuff for the unit carrying it.
	///        An explicit Negative flag always wins. Otherwise an aura counts as a debuff if the
	///        spell aims an aura at enemies and was not cast by the aura's owner on itself (so a
	///        spell which buffs its caster and debuffs its target only marks the target's aura).
	/// @param spell The aura's spell.
	/// @param casterId Guid of the unit which applied the aura.
	/// @param ownerId Guid of the unit carrying the aura.
	template<typename TSpellEntry>
	bool IsNegativeAura(const TSpellEntry& spell, const uint64 casterId, const uint64 ownerId)
	{
		if (HasNegativeAttribute(spell))
		{
			return true;
		}

		return casterId != ownerId && SpellAppliesHostileAura(spell);
	}
}
