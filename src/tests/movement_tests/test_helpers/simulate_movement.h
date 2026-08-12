#pragma once

#include "game_client/game_unit_c.h"
#include "math/vector3.h"
#include <functional>

namespace mmo
{
	/// @brief Helper for movement tests: accumulates an input vector then ticks movement.
	/// Calls AddInputVector then unit.GetUnitMovement()->Tick(dt) for `steps` iterations.
	inline void SimulateMovement(GameUnitC& unit, const Vector3& inputVec, int steps, float dt)
	{
		UnitMovement* movement = unit.GetUnitMovement();
		if (!movement)
		{
			return;
		}

		for (int i = 0; i < steps; ++i)
		{
			unit.AddInputVector(inputVec);
			movement->Tick(dt);
		}
	}

	/// @brief Overload with a per-step callback invoked after each tick.
	/// Callback receives (stepIndex, position, velocity) so tests can track peak values.
	inline void SimulateMovement(GameUnitC& unit, const Vector3& inputVec, int steps, float dt,
		const std::function<void(int, const Vector3&, const Vector3&)>& onStep)
	{
		UnitMovement* movement = unit.GetUnitMovement();
		if (!movement)
		{
			return;
		}

		for (int i = 0; i < steps; ++i)
		{
			unit.AddInputVector(inputVec);
			movement->Tick(dt);
			if (onStep)
			{
				onStep(i, unit.GetPosition(), movement->GetVelocity());
			}
		}
	}
}
