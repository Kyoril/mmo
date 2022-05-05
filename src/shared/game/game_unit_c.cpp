
#include "game_unit_c.h"

namespace mmo
{
	void GameUnitC::StartMove(const bool forward)
	{
		if (forward)
		{
			m_movementInfo.movementFlags |= MovementFlags::Forward;
			m_movementInfo.movementFlags &= ~MovementFlags::Backward;
		}
		else
		{
			m_movementInfo.movementFlags |= MovementFlags::Backward;
			m_movementInfo.movementFlags &= ~MovementFlags::Forward;
		}
	}

	void GameUnitC::StopMove()
	{
		m_movementInfo.movementFlags &= ~MovementFlags::Moving;
	}

	void GameUnitC::StartTurn(const bool left)
	{
		if (left)
		{
			m_movementInfo.movementFlags |= MovementFlags::TurnLeft;
			m_movementInfo.movementFlags &= ~MovementFlags::TurnRight;
		}
		else
		{
			m_movementInfo.movementFlags |= MovementFlags::TurnRight;
			m_movementInfo.movementFlags &= ~MovementFlags::TurnLeft;
		}
	}

	void GameUnitC::StopTurn()
	{
		m_movementInfo.movementFlags &= ~MovementFlags::Turning;
	}

	void GameUnitC::SetFacing(const Radian& facing)
	{
	}
}
