// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/trigger_handler.h"

namespace mmo
{
	TriggerContext::TriggerContext(GameObjectS* owner_, GameUnitS* triggering)
		: owner(owner_)
		, spellHitId(0)
	{
		if (triggering)
		{
			triggeringUnit = std::static_pointer_cast<GameUnitS>(triggering->shared_from_this());
		}
	}
}