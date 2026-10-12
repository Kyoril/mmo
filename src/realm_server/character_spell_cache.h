// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <vector>

namespace mmo
{
	class GamePlayerS;

	/// Collects the spell ids the realm caches for a character's next world node: every known spell,
	/// including ones inactive for the current class or waiting for their talent.
	///
	/// @param character The character as reported by its current world node.
	/// @returns The spell ids to hand to the next world node.
	std::vector<uint32> CollectCachedSpellIds(const GamePlayerS& character);
}
