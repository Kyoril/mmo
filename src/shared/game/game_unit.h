// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "game_object.h"

namespace mmo
{
	/// @brief Represents a living object (unit) in the game world.
	class GameUnit : public GameObject
	{
	public:

		GameUnit(const ObjectGuid guid)
			: GameObject(guid)
		{
		}

		virtual ~GameUnit() override = default;

	public:

	};
}
