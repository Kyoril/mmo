#pragma once

#include "game_unit_s.h"

namespace mmo
{
	/// @brief Represents a playable character in the game world.
	class GamePlayer final : public GameUnitS
	{
	public:
		GamePlayer(const ObjectGuid guid)
			: GameUnitS(guid)
		{
		}

		~GamePlayer() override = default;

	protected:
		void PrepareFieldMap() override
		{
			m_fields.Initialize(object_fields::PlayerFieldCount);
		}
	};
}
