#pragma once

#include "game_unit.h"

namespace mmo
{
	/// @brief Represents a playable character in the game world.
	class GamePlayer final : public GameUnit
	{
	public:
		GamePlayer(const ObjectGuid guid)
			: GameUnit(guid)
		{
		}

		~GamePlayer() override = default;
	};
}