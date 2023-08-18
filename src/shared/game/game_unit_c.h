#pragma once

#include "game_object_c.h"
#include "movement_info.h"

namespace mmo
{
	class GameUnitC : public GameObjectC
	{
	public:
		explicit GameUnitC(Scene& scene)
			: GameObjectC(scene)
		{
		}

		virtual ~GameUnitC() override = default;

	public:
		void StartMove(bool forward);
		
		void StartStrafe(bool left);

		void StopMove();

		void StopStrafe();

		void StartTurn(bool left);

		void StopTurn();

		void SetFacing(const Radian& facing);

	public:
		[[nodiscard]] const MovementInfo& GetMovementInfo() const noexcept { return m_movementInfo; }

	protected:
		MovementInfo m_movementInfo;
	};
}
