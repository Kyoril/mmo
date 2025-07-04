
#include "movement.h"

#include "event_loop.h"
#include "base/clock.h"
#include "game_client/object_mgr.h"

namespace mmo
{
	void Movement::Initialize()
	{
		// From now on, we will be called every update and move all movers
		m_lastMovementUpdate = 0;
		m_movementIdle = EventLoop::Idle.connect(this, &Movement::OnMovementIdle);
	}

	void Movement::Terminate()
	{
		// Stop processing movement updates
		m_movementIdle.disconnect();
	}

	void Movement::OnMovementIdle(float deltaSeconds, GameTime timestamp)
	{
		// Check if we have any movers at all
		if (!HasMovers())
		{
			// No movers, so nothing to do, but we remember when we were last called
			m_lastMovementUpdate = timestamp;
			return;
		}

		const bool movementIsNow = timestamp == m_lastMovementUpdate;
		ASSERT(!movementIsNow);

		if (movementIsNow)
		{
			m_lastMovementUpdate = timestamp;
			return;
		}

		GameTime timeDiff = timestamp - m_lastMovementUpdate;
		do
		{
			const GameTime lastUpdate = m_lastMovementUpdate;
			if (timeDiff <= constants::OneSecond)
			{
				m_lastMovementUpdate = timestamp;
				timeDiff = 0;
				MoveUnits(timestamp, m_lastMovementUpdate - lastUpdate);
			}
			else
			{
				m_lastMovementUpdate = lastUpdate + constants::OneSecond;
				timeDiff -= constants::OneSecond;
			}

			// If we have a local mover
			if (HasLocalMover())
			{
				// MoveLocalPlayer
				MoveLocalPlayer(timestamp, m_lastMovementUpdate - lastUpdate);
			}
		} while (timeDiff > 0);
	}

	bool Movement::HasMovers() const
	{
		// TODO: Check if we have any movers at all
		return HasLocalMover() || false;
	}

	bool Movement::HasLocalMover() const
	{
		return ObjectMgr::GetActivePlayerGuid() != 0;
	}

	void Movement::MoveUnits(GameTime timestamp, GameTime timeDiff)
	{
		const float deltaSeconds = timeDiff * 0.001f;

		ObjectMgr::ForEachUnit([deltaSeconds](GameUnitC& unit)
		{
			// Don't move local player just yet
			if (unit.GetGuid() == ObjectMgr::GetActivePlayerGuid())
			{
				// Skip the local player, we will move them separately
				return;
			}

			unit.ApplyLocalMovement(deltaSeconds);
		});
	}

	void Movement::MoveLocalPlayer(GameTime timestamp, GameTime timeDiff)
	{
		std::shared_ptr<GamePlayerC> player = ObjectMgr::GetActivePlayer();
		ASSERT(player);

		// Apply movement updates to the local player
		player->ApplyLocalMovement(timeDiff * 0.001f);
	}
}
