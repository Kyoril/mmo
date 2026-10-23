// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#pragma once

namespace mmo
{
	/// Social interactions require the same underlying faction, independently of duel hostility.
	/// Missing faction data fails closed until the authoritative template is known.
	template<class FactionTemplate>
	bool CanPlayerFactionsInteract(const FactionTemplate* first, const FactionTemplate* second)
	{
		return first && second && first->faction() == second->faction();
	}
}
