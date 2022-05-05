
#pragma once

#include "base/clock.h"
#include "math/vector3.h"

#include <queue>

namespace mmo
{	
	enum class MovementEventType
	{
		None,

		StartMoveForward,
		StartMoveBackward,
		StopMove,
		StartStrafeLeft,
		StartStrafeRight,
		StopStrafe,
		StartTurnLeft,
		StartTurnRight,
		StopTurn,
		Jump,
	};
	
	struct MovementEvent
	{
		MovementEventType type;
		GameTime timestamp;
		Vector3 position;
		Radian facing;
	};
	
	class Movement
	{
	public:
		explicit Movement()
		{
		}

		virtual ~Movement()
		{
		}

	public:
		void AddMovementEvent(MovementEvent&& movementEvent)
		{
			m_movementEvents.emplace(std::move(movementEvent));
		}

		void AddMovementEvent(const MovementEvent& movementEvent)
		{
			m_movementEvents.push(movementEvent);
		}

	protected:
		std::queue<MovementEvent> m_movementEvents;
	};
}
