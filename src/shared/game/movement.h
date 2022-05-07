
#pragma once

#include <list>

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
			// Insert event into m_movementEvents sorted by movementEvent.timestamp
			for (auto it = m_movementEvents.begin(); it != m_movementEvents.end(); ++it)
			{
				if (it->timestamp < movementEvent.timestamp)
				{
					continue;
				}

				m_movementEvents.insert(it, std::move(movementEvent));
				return;
			}

			m_movementEvents.emplace_back(std::move(movementEvent));
		}

		void AddMovementEvent(const MovementEvent& movementEvent)
		{
			// Insert event into m_movementEvents sorted by movementEvent.timestamp
			for (auto it = m_movementEvents.begin(); it != m_movementEvents.end(); ++it)
			{
				if (it->timestamp < movementEvent.timestamp)
				{
					continue;
				}

				m_movementEvents.insert(it, movementEvent);
				return;
			}

			m_movementEvents.push_back(movementEvent);
		}
		
	protected:
		std::list<MovementEvent> m_movementEvents;
	};
}
