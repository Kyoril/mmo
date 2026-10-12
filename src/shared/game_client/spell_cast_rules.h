// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/spell.h"
#include "game/spell_target_map.h"

namespace mmo::spell_cast_rules
{
	/// \brief Whether moving keeps the player from starting a spell cast.
	///
	/// Movement only interrupts a cast that is still in progress (the server's
	/// SingleCastState::OnUserStartsMoving), so a spell whose cast time spell mods have
	/// reduced to zero resolves at once and may be cast on the run. Channels keep running
	/// after the cast itself and are interrupted by movement, so they stay blocked.
	/// \param interruptFlags The spell's interrupt flags, see spell_interrupt_flags.
	/// \param attributes The spell's first attribute set, see spell_attributes.
	/// \param effectiveCastTimeMs The cast time after the caster's spell mods were applied.
	/// \param isMoving Whether the caster is currently changing its position.
	inline bool IsBlockedByMovement(const uint32 interruptFlags, const uint32 attributes, const int32 effectiveCastTimeMs, const bool isMoving)
	{
		if (!isMoving || (interruptFlags & spell_interrupt_flags::Movement) == 0)
		{
			return false;
		}

		return effectiveCastTimeMs > 0 || (attributes & spell_attributes::Channeled) != 0;
	}
}
