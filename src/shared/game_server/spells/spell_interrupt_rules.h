// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game/spell.h"
#include "shared/proto_data/spells.pb.h"

namespace mmo
{
	/// Whether interrupt effects (kicks) are unable to stop a cast or channel of this spell.
	/// @param spell The spell being cast or channeled.
	/// @return True if the spell carries spell_attributes_b::CannotBeInterrupted.
	inline bool IsUninterruptible(const proto::SpellEntry& spell)
	{
		return spell.attributes_size() >= 2 && (spell.attributes(1) & spell_attributes_b::CannotBeInterrupted) != 0;
	}
}
