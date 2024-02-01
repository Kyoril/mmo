// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "game_object_s.h"

namespace mmo
{
	/// @brief Represents a living object (unit) in the game world.
	class GameUnitS : public GameObjectS
	{
	public:

		GameUnitS(const ObjectGuid guid)
			: GameObjectS(guid)
		{
		}

		virtual ~GameUnitS() override = default;

	protected:
		virtual void PrepareFieldMap() override
		{
			m_fields.Initialize(object_fields::UnitFieldCount);
		}

	public:
	};
}
