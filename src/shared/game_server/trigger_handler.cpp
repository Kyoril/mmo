// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/trigger_handler.h"

namespace mmo
{
	TriggerContext::TriggerContext(GameObjectS* owner_, GameUnitS* triggering, WorldInstance* world_)
		: owner(owner_)
		, world(world_ ? world_ : (owner_ ? owner_->GetWorldInstance() : nullptr))
		, spellHitId(0)
	{
		if (triggering)
		{
			triggeringUnit = std::static_pointer_cast<GameUnitS>(triggering->shared_from_this());
		}
	}
}