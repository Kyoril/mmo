// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "entity_edit_mode.h"

namespace mmo
{
	EntityEditMode::EntityEditMode(IWorldEditor& worldEditor)
		: WorldEditMode(worldEditor)
	{
	}

	const char* EntityEditMode::GetName() const
	{
		static const char* s_name = "Static Map Entities";
		return s_name;
	}

	void EntityEditMode::DrawDetails()
	{
	}
}
