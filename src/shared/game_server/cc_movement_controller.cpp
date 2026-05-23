// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "cc_movement_controller.h"

#include "objects/game_unit_s.h"
#include "unit_mover.h"
#include "world/world_instance.h"
#include "base/clock.h"
#include "game/movement_type.h"

namespace mmo
{
	CCMovementController::CCMovementController(GameUnitS& unit)
		: m_unit(unit)
		, m_wanderCountdown(unit.GetTimers())
	{
	}

	void CCMovementController::Start()
	{
		if (m_active)
			return;

		m_active = true;

		// If the unit is currently suppressed, don't move yet — OnWanderTick
		// will be called once the suppression ends.
		if (m_unit.IsStunned() || m_unit.IsSleeping() || m_unit.IsRooted())
			return;

		// Arm the first wander tick immediately
		m_wanderCountdown.ended.connect(this, &CCMovementController::OnWanderTick);
		m_wanderCountdown.SetEnd(GetAsyncTimeMs() + kWanderTickMs);
	}

	void CCMovementController::Stop()
	{
		m_active = false;
		m_wanderCountdown.Cancel();
		m_targetReachedConn.disconnect();
		m_unit.GetMover().StopMovement();
	}

	void CCMovementController::OnWanderTick()
	{
		if (!m_active)
			return;

		// Suppressed — stop locomotion and re-arm so we check again later
		if (m_unit.IsStunned() || m_unit.IsSleeping() || m_unit.IsRooted())
		{
			m_unit.GetMover().StopMovement();
			m_wanderCountdown.SetEnd(GetAsyncTimeMs() + kWanderTickMs);
			return;
		}

		PickNextWanderPoint();
	}

	void CCMovementController::PickNextWanderPoint()
	{
		WorldInstance* world = m_unit.GetWorldInstance();
		if (!world)
		{
			// No world — stand still and retry later
			m_wanderCountdown.SetEnd(GetAsyncTimeMs() + kWanderTickMs);
			return;
		}

		MapData* mapData = world->GetMapData();
		if (!mapData)
		{
			// No navmesh — stand still
			m_wanderCountdown.SetEnd(GetAsyncTimeMs() + kWanderTickMs);
			return;
		}

		float radius;
		float speed;

		if (m_unit.IsFeared())
		{
			radius = kFearRadius;
			speed = m_unit.GetSpeed(movement_type::Run);
		}
		else
		{
			// Disoriented
			radius = kDisorientRadius;
			speed = m_unit.GetSpeed(movement_type::Run) * 0.6f;
		}

		Vector3 destination;
		if (!mapData->FindRandomPointAroundCircle(m_unit.GetPosition(), radius, destination))
		{
			// No valid navmesh point — stand still, retry next tick
			m_wanderCountdown.SetEnd(GetAsyncTimeMs() + kWanderTickMs);
			return;
		}

		// Disconnect previous target-reached connection before issuing new move
		m_targetReachedConn.disconnect();

		UnitMover& mover = m_unit.GetMover();
		if (mover.MoveTo(destination, speed, 0.5f))
		{
			m_targetReachedConn = mover.targetReached.connect([this]()
			{
				if (m_active)
				{
					m_wanderCountdown.SetEnd(GetAsyncTimeMs() + kWanderTickMs);
				}
			});
		}
		else
		{
			// MoveTo failed — re-arm countdown
			m_wanderCountdown.SetEnd(GetAsyncTimeMs() + kWanderTickMs);
		}
	}
}
