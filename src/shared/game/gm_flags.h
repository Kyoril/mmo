// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/gm_level.h"

namespace mmo
{
	/// Per-character game master state that survives map changes and relogs. Stored in the
	/// realm database (characters.gm_flags) and carried to world nodes in CharacterData.
	namespace gm_flags
	{
		enum Type : uint8
		{
			/// No game master state active.
			None = 0,

			/// The character is invisible to players and creatures (other game masters still see it).
			Invisible = 1 << 0,

			/// The character ignores all incoming damage.
			Godmode = 1 << 1,

			/// Every known flag.
			All = Invisible | Godmode,
		};
	}

	/// Returns the flags that may actually apply for an account of the given gm level. Each flag
	/// survives only at the level that may toggle it, so a demoted account never keeps state it
	/// can no longer clear: godmode needs gm_level::SeniorGm, invisibility gm_level::Gm, and a
	/// regular player account keeps nothing. Unknown bits are dropped.
	/// @param flags The stored flags.
	/// @param gmLevel The account's gm level (gm_level::Type).
	/// @returns The effective flags.
	inline uint8 SanitizeGmFlags(const uint8 flags, const uint8 gmLevel)
	{
		if (gmLevel < gm_level::Gm)
		{
			return gm_flags::None;
		}

		uint8 result = flags & gm_flags::All;
		if (gmLevel < gm_level::SeniorGm)
		{
			result &= static_cast<uint8>(~gm_flags::Godmode);
		}

		return result;
	}
}
