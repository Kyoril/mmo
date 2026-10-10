// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

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

	/// Returns the flags that may actually apply for an account of the given gm level: none for
	/// a regular player account (a demoted account must never stay hidden), unknown bits dropped.
	/// @param flags The stored flags.
	/// @param gmLevel The account's gm level (gm_level::Type).
	/// @returns The effective flags.
	inline uint8 SanitizeGmFlags(const uint8 flags, const uint8 gmLevel)
	{
		if (gmLevel == 0)
		{
			return gm_flags::None;
		}

		return flags & gm_flags::All;
	}
}
