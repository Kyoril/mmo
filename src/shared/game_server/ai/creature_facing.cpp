// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/ai/creature_facing.h"

#include "game_server/objects/game_unit_s.h"

namespace mmo
{
	bool CanTurnToFaceTarget(const GameUnitS& unit)
	{
		if (!unit.IsAlive())
		{
			return false;
		}

		// Root is intentionally absent: a rooted unit cannot move but can still turn.
		return !unit.IsStunned()
			&& !unit.IsSleeping()
			&& !unit.IsFeared()
			&& !unit.IsDisoriented();
	}
}
