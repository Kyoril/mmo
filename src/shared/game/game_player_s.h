#pragma once

#include "game_unit_s.h"

namespace mmo
{
	/// @brief Represents a playable character in the game world.
	class GamePlayerS final : public GameUnitS
	{
	public:
		GamePlayerS(const ObjectGuid guid)
			: GameUnitS(guid)
		{
		}

		~GamePlayerS() override = default;

	protected:
		void PrepareFieldMap() override
		{
			m_fields.Initialize(object_fields::PlayerFieldCount);
		}
	};
}
