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

		// Capture fear-source context so PickNextWanderPoint can flee directionally.
		m_fearOrigin = m_unit.GetPosition();
		m_hasFearSource = false;

		// Prefer the unit that this NPC is currently fighting (most likely the caster).
		// Fall back to the first attacker if no victim is set.
		if (const GameUnitS* victim = m_unit.GetVictim())
		{
			m_fearSourcePos = victim->GetPosition();
			m_hasFearSource = true;
		}
		else
		{
			m_unit.ForEachAttacker([&](const GameUnitS& attacker)
			{
				if (!m_hasFearSource)
				{
					m_fearSourcePos = attacker.GetPosition();
					m_hasFearSource = true;
				}
			});
		}

		// If the unit is currently suppressed, don't move yet — OnWanderTick
		// will be called once the suppression ends.
		if (m_unit.IsStunned() || m_unit.IsSleeping() || m_unit.IsRooted())
			return;

		// Connect the countdown for future ticks (used when movement reaches its
		// destination or when suppression is lifted).
		m_wanderCountdown.ended.connect(this, &CCMovementController::OnWanderTick);

		// Kick off the first flee move immediately — no initial delay.
		PickNextWanderPoint();
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

		// Suppressed — stop locomotion and re-arm so we check again later.
		if (m_unit.IsStunned() || m_unit.IsSleeping() || m_unit.IsRooted())
		{
			m_unit.GetMover().StopMovement();
			m_wanderCountdown.SetEnd(GetAsyncTimeMs() + kWanderTickMs);
			return;
		}

		// Safety timeout fired — disconnect any pending targetReached so it
		// doesn't also call PickNextWanderPoint after we do.
		m_targetReachedConn.disconnect();
		PickNextWanderPoint();
	}

	void CCMovementController::PickNextWanderPoint()
	{
		WorldInstance* world = m_unit.GetWorldInstance();
		if (!world)
		{
			m_wanderCountdown.SetEnd(GetAsyncTimeMs() + kWanderTickMs);
			return;
		}

		MapData* mapData = world->GetMapData();
		if (!mapData)
		{
			m_wanderCountdown.SetEnd(GetAsyncTimeMs() + kWanderTickMs);
			return;
		}

		const float speed = m_unit.GetSpeed(movement_type::Run);
		Vector3 destination;
		bool found = false;

		if (m_unit.IsFeared())
		{
			// --- Directional fear movement ---
			// Compute a flee direction: away from the fear source if known,
			// otherwise away from the unit's own fear-start position (so it at
			// least keeps moving outward rather than doubling back).
			const Vector3 currentPos = m_unit.GetPosition();

			Vector3 fleeDir;
			if (m_hasFearSource)
			{
				// Away from the caster's position captured at fear-start.
				const Vector3 toSource = m_fearSourcePos - currentPos;
				if (!toSource.IsZeroLength())
					fleeDir = (toSource * -1.0f).NormalizedCopy();
				else
					fleeDir = (currentPos - m_fearOrigin).IsZeroLength()
					          ? Vector3(1.0f, 0.0f, 0.0f)
					          : (currentPos - m_fearOrigin).NormalizedCopy();
			}
			else
			{
				// No caster info — flee away from the fear-start origin.
				const Vector3 fromOrigin = currentPos - m_fearOrigin;
				if (!fromOrigin.IsZeroLength())
					fleeDir = fromOrigin.NormalizedCopy();
				else
					fleeDir = Vector3(1.0f, 0.0f, 0.0f); // arbitrary fallback
			}

			// Project a target point kFearProjectionDist ahead in the flee direction
			// and sample the navmesh around it.  This produces movement of ~12 m per
			// tick, strongly biased away from the caster.
			const Vector3 projectedCenter = currentPos + fleeDir * kFearProjectionDist;
			found = mapData->FindRandomPointAroundCircle(projectedCenter, kFearSampleRadius, destination);

			if (!found)
			{
				// Navmesh blocked ahead (wall, cliff, water) — try a wider sample
				// centred on the unit itself so it still moves somewhere.
				found = mapData->FindRandomPointAroundCircle(currentPos, kFearFallbackRadius, destination);
			}
		}
		else
		{
			// Disorient: small random wander around current position.
			found = mapData->FindRandomPointAroundCircle(m_unit.GetPosition(), kDisorientRadius, destination);
		}

		if (!found)
		{
			m_wanderCountdown.SetEnd(GetAsyncTimeMs() + kWanderTickMs);
			return;
		}

		// Disconnect previous target-reached connection before issuing new move.
		m_targetReachedConn.disconnect();

		UnitMover& mover = m_unit.GetMover();
		if (mover.MoveTo(destination, speed, 0.5f))
		{
			// Chain the next flee point immediately on arrival — no pause between legs.
			m_targetReachedConn = mover.targetReached.connect([this]()
			{
				if (m_active)
				{
					// Cancel the safety countdown so it doesn't double-fire.
					m_wanderCountdown.Cancel();
					PickNextWanderPoint();
				}
			});

			// Safety countdown: if the unit gets stuck and targetReached never fires,
			// force a new point after kWanderTickMs.
			m_wanderCountdown.SetEnd(GetAsyncTimeMs() + kWanderTickMs);
		}
		else
		{
			// MoveTo failed — retry after a short delay.
			m_wanderCountdown.SetEnd(GetAsyncTimeMs() + kWanderTickMs);
		}
	}
}
