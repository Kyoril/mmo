// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "shared/proto_data/project.h"

#include <vector>

namespace mmo
{
	/// Appends the racial spells of a race to a character's spell list.
	///
	/// Racial spells are class-independent: every character of the race knows them. Spells
	/// already in the list are not added twice, and ids missing from the spell manager are
	/// skipped so stale race data cannot hand out spells that do not exist.
	///
	/// @param race The race whose racialSpells list is merged.
	/// @param spells The spell manager used to validate the ids.
	/// @param spellIds The character's spell list; receives the missing racial spells in list order.
	/// @return The ids that were appended.
	std::vector<uint32> AppendRacialSpells(const proto::RaceEntry& race, const proto::SpellManager& spells, std::vector<uint32>& spellIds);

	/// Determines whether a spell is placed on a fresh action bar automatically: a visible,
	/// non-passive spell with the Ability attribute.
	///
	/// @param spell The spell to check.
	/// @return true if the spell belongs on a default action bar.
	bool IsActionBarAbility(const proto::SpellEntry& spell);
}
