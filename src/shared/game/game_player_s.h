#pragma once

#include "game_unit_s.h"

namespace mmo
{
	/// @brief Represents a playable character in the game world.
	class GamePlayerS final : public GameUnitS
	{
	public:
		GamePlayerS(const proto::Project& project, TimerQueue& timerQueue)
			: GameUnitS(project, timerQueue)
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
