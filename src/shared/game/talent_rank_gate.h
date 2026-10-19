// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// Finds the talent a spell rank depends on.
	///
	/// Spell ranks are chained through `baseid` (rank 2 points at rank 1). When the spell a rank points
	/// at is itself the rank spell of a talent, the rank is a follow-up of that talent: it is bought from
	/// a trainer but only usable while the talent is learned. Works with server and client projects,
	/// whose template managers share the same interface.
	///
	/// @param spell The spell rank to check.
	/// @param spells The spell manager to resolve the base spell from.
	/// @param talents The talent manager to search.
	/// @returns The gating talent, or nullptr when the spell is not a follow-up rank of a talent spell.
	template<typename SpellEntryT, typename SpellManagerT, typename TalentManagerT>
	auto FindGatingTalent(const SpellEntryT& spell, const SpellManagerT& spells, const TalentManagerT& talents)
		-> decltype(talents.getById(0u))
	{
		const uint32 baseId = spell.baseid();
		if (baseId == 0 || baseId == spell.id())
		{
			return nullptr;
		}

		if (spells.getById(baseId) == nullptr)
		{
			return nullptr;
		}

		for (const auto& talent : talents.getTemplates().entry())
		{
			for (const uint32 rankSpellId : talent.ranks())
			{
				if (rankSpellId == baseId)
				{
					return talents.getById(talent.id());
				}
			}
		}

		return nullptr;
	}
}
