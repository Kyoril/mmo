// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "character_spell_cache.h"

#include "game_server/objects/game_player_s.h"

namespace mmo
{
	std::vector<uint32> CollectCachedSpellIds(const GamePlayerS& character)
	{
		// The known set, not the active one: spells of inactive classes and dormant talent follow-up
		// ranks must reach the next world node, which decides again what is active. Talent spells are
		// not in it; they are restored from the per-class talent data.
		const auto& known = character.GetKnownSpellIds();
		return { known.begin(), known.end() };
	}
}
