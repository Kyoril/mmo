#pragma once
#include "game_bag_c.h"

namespace mmo
{
	namespace player_move_event
	{
		enum Type
		{
			MoveStartForward,
			MoveStartBackward,
			MoveStop,
			StrafeStartLeft,
			StrafeStartRight,
			StrafeStop,
			Fall,
			Jump,
			TurnStartLeft,
			TurnStartRight,
			TurnStop,
			PitchStartUp,
			PitchStartDown,
			PitchStop,
			SetRunMode,
			SetWalkMode,
			SetFacing,
			SetPitch,
			MoveStartSwim,
			MoveStopSwim,

			Count_
		};
	}

	typedef player_move_event::Type PlayerMoveEventType;

	/// A single move event.
	struct PlayerMoveEvent
	{
		GameTime timeStamp;
		PlayerMoveEventType eventType;
		Radian facing;
	};
}
