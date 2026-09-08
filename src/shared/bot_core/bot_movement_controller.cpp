// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "bot_movement_controller.h"

#include <cmath>

#include "bot_context.h"
#include "bot_movement_math.h"
#include "bot_nav_service.h"
#include "bot_unit.h"

#include "base/macros.h"
#include "game/movement_type.h"
#include "game_protocol/game_protocol.h"
#include "log/default_log_levels.h"

#include <sstream>
#include <utility>

namespace mmo
{
	namespace
	{
		/// How close the bot has to be, horizontally, to treat itself as standing at its target and
		/// skip the navigation query. Small on purpose: this is "I am here", not "near enough".
		constexpr float StandingOnTargetPlanarDistance = 1.0f;

		/// And how little height may separate them. Without this the check cannot tell a target at
		/// arm's length from one on the floor below.
		constexpr float StandingOnTargetHeightDifference = 2.0f;
	}

	namespace
	{
		std::size_t FindInitialWaypointIndex(const std::vector<Vector3>& path)
		{
			return path.size() > 1 ? 1u : 0u;
		}
	}

	BotMovementController::BotMovementController(BotMovementSettings settings)
		: m_settings(std::move(settings))
	{
		if (m_settings.acceptanceRadius < 0.0f)
		{
			m_settings.acceptanceRadius = 0.0f;
		}
		if (m_settings.waypointAcceptanceRadius < 0.0f)
		{
			m_settings.waypointAcceptanceRadius = 0.0f;
		}
	}

	bool BotMovementController::MoveTo(BotContext& context, const Vector3& target, const float acceptanceRadius)
	{
		StopLowLevel(context, "replaced", false);
		Reset();

		const float effectiveAcceptance = acceptanceRadius >= 0.0f ? acceptanceRadius : m_settings.acceptanceRadius;
		if (PlanPath(context, target, effectiveAcceptance))
		{
			m_settings.acceptanceRadius = effectiveAcceptance;
			SetStatus(BotMovementStatus::Moving, "path_ready");
			MovementStarted(BuildEvent(context, m_status, m_lastReason));
			return true;
		}

		EmitUnreachable(context, m_lastReason.empty() ? "path_unavailable" : m_lastReason);
		return false;
	}

	BotMovementStatus BotMovementController::Update(BotContext& context)
	{
		if (m_status != BotMovementStatus::Moving)
		{
			return m_status;
		}

		MovementInfo movement = context.GetMovementInfo();
		if ((movement.movementFlags & movement_flags::Falling) != 0)
		{
			context.SendLandedPacket();
			movement = context.GetMovementInfo();
		}

		const float distanceToGoal = PlanarDistance(movement.position, m_target);
		if (distanceToGoal <= m_settings.acceptanceRadius)
		{
			EmitReached(context, "target_reached");
			return m_status;
		}

		// Resolving the waypoint and the point to steer at together keeps the two in step: the
		// steering target the bot stops on is the same point that decides the waypoint is reached.
		const BotPathFollowState follow = AdvanceBotPathFollowing(
			m_path,
			m_nextWaypointIndex,
			movement.position,
			m_settings.waypointAcceptanceRadius,
			m_settings.turnSmoothingThresholdRadians,
			m_settings.turnSmoothingDistance);

		// Bounded by the live path as well as by the resolved index: WaypointAdvanced is a public
		// signal, and a subscriber is free to call Stop() or MoveTo() and leave m_path empty
		// underneath us.
		while (m_nextWaypointIndex < follow.waypointIndex && m_nextWaypointIndex < m_path.size())
		{
			++m_nextWaypointIndex;
			WaypointAdvanced(BuildEvent(context, m_status, "waypoint_reached"));
		}

		if (follow.exhausted)
		{
			EmitUnreachable(context, "path_exhausted");
			return m_status;
		}

		const GameTime now = context.GetServerTime();
		if (m_runtime.isMoving
			&& m_runtime.hasLastProgressPosition
			&& now >= m_runtime.lastProgressTime
			&& now - m_runtime.lastProgressTime >= m_settings.nonProgressTimeoutMs
			&& PlanarDistance(m_runtime.lastProgressPosition, movement.position) < m_settings.progressDistanceEpsilon)
		{
			// Guaranteed by the exhaustion check above, and the log below relies on it.
			ASSERT(m_nextWaypointIndex < m_path.size());

			// A stall says nothing about where it happened, which is what any investigation needs
			// first - so spell the geometry out while the state is still around.
			WLOG("Bot movement stalled at (" << movement.position.x << ", " << movement.position.y << ", " << movement.position.z
				<< ") heading for waypoint " << m_nextWaypointIndex << " of " << m_path.size()
				<< " at (" << m_path[m_nextWaypointIndex].x << ", " << m_path[m_nextWaypointIndex].y << ", " << m_path[m_nextWaypointIndex].z
				<< "), steering at (" << follow.steeringTarget.x << ", " << follow.steeringTarget.y << ", " << follow.steeringTarget.z
				<< "), distance " << PlanarDistance(movement.position, follow.steeringTarget)
				<< " with acceptance radius " << m_settings.waypointAcceptanceRadius);

			SetStatus(BotMovementStatus::Stuck, "non_progress");
			TargetUnreachable(BuildEvent(context, m_status, m_lastReason));
			StopLowLevel(context, m_lastReason, true);
			return m_status;
		}

		const Vector3 steeringTarget = follow.steeringTarget;
		movement.facing = ComputeFacingTo(movement.position, steeringTarget, movement.facing);

		if (!m_runtime.isMoving && !context.IsMoving())
		{
			movement.movementFlags |= movement_flags::Forward;
			movement.timestamp = now;
			context.SendMovementUpdate(game::client_realm_packet::MoveStartForward, movement);
			m_runtime.isMoving = true;
			m_runtime.lastSimulationTime = now;
			m_runtime.lastHeartbeatTime = now;
			m_runtime.lastProgressPosition = movement.position;
			m_runtime.lastProgressTime = now;
			m_runtime.hasLastProgressPosition = true;
			return m_status;
		}

		BotLowLevelMovementInput lowLevelInput;
		lowLevelInput.movement = movement;
		lowLevelInput.runtime = m_runtime;
		lowLevelInput.steeringTarget = steeringTarget;
		lowLevelInput.now = now;
		lowLevelInput.maxSpeed = ResolveRunSpeed(context);
		lowLevelInput.maxAcceleration = m_settings.maxAcceleration;
		lowLevelInput.acceptanceRadius = m_settings.waypointAcceptanceRadius;

		const BotLowLevelMovementOutput lowLevelOutput = AdvanceBotLowLevelMovement(lowLevelInput);
		movement = lowLevelOutput.movement;
		m_runtime = lowLevelOutput.runtime;
		m_runtime.isMoving = true;
		movement.movementFlags |= movement_flags::Forward;
		context.UpdateMovementInfo(movement);

		if (now >= m_runtime.lastHeartbeatTime && now - m_runtime.lastHeartbeatTime >= m_settings.heartbeatIntervalMs)
		{
			context.SendMovementUpdate(game::client_realm_packet::MoveHeartBeat, movement);
			m_runtime.lastHeartbeatTime = now;
		}

		return m_status;
	}

	void BotMovementController::Stop(BotContext& context, std::string reason)
	{
		StopLowLevel(context, std::move(reason), true);
		Reset();
	}

	void BotMovementController::Reset()
	{
		m_runtime = {};
		m_path.clear();
		m_target = Vector3::Zero;
		m_mapId = 0;
		m_nextWaypointIndex = 0;
		SetStatus(BotMovementStatus::Idle, {});
	}

	bool BotMovementController::PlanPath(BotContext& context, const Vector3& target, const float acceptanceRadius)
	{
		if (!context.IsWorldReady())
		{
			SetStatus(BotMovementStatus::Unreachable, "world_not_ready");
			return false;
		}

		if (!context.HasAuthoritativeMovementInfo() && context.GetSelf() == nullptr)
		{
			SetStatus(BotMovementStatus::Unreachable, "self_unavailable");
			return false;
		}

		if (!IsFiniteVector(target))
		{
			SetStatus(BotMovementStatus::Unreachable, "invalid_target");
			return false;
		}

		const Vector3 start = context.GetPosition();

		// Skipping the navigation query is a claim that the bot is already standing at the target,
		// so it is measured against being there - not against whatever tolerance the caller asked
		// for. Gating it on acceptanceRadius meant a 15 yard arrival radius licensed a 15 yard walk
		// with no navigation mesh consulted at all, and because the test ignored height, a creature
		// directly below a floor counted as within it. Bots walked through the wall of a house to
		// reach the rats in its cellar, and the server let them: player movement is
		// client-authoritative, so nothing downstream was going to object.
		if (PlanarDistance(start, target) <= StandingOnTargetPlanarDistance
			&& std::abs(start.y - target.y) <= StandingOnTargetHeightDifference)
		{
			m_path = { start, target };
			m_target = target;
			m_mapId = context.GetCurrentMapId();
			m_nextWaypointIndex = FindInitialWaypointIndex(m_path);
			return true;
		}

		BotNavService* navService = context.GetNavService();
		if (!navService || !navService->IsReady())
		{
			SetStatus(BotMovementStatus::Unreachable, "nav_unavailable");
			return false;
		}

		uint32 mapId = context.GetCurrentMapId();
		if (mapId == 0)
		{
			if (const auto inferredMapId = navService->InferMapId(start, target); inferredMapId.has_value())
			{
				mapId = *inferredMapId;
				context.SetCurrentMapId(mapId);
			}
		}

		const BotPathResult result = navService->FindPath(mapId, start, target);
		if (!result.success)
		{
			SetStatus(BotMovementStatus::Unreachable, result.reason.empty() ? "invalid_path" : result.reason);
			return false;
		}

		m_path = result.points;
		m_target = target;
		m_mapId = mapId;
		m_nextWaypointIndex = FindInitialWaypointIndex(m_path);
		return true;
	}

	float BotMovementController::ResolveRunSpeed(const BotContext& context) const
	{
		if (const BotUnit* self = context.GetSelf())
		{
			return self->GetSpeed(movement_type::Run);
		}

		return m_settings.fallbackRunSpeed;
	}

	BotMovementEvent BotMovementController::BuildEvent(BotContext& context, const BotMovementStatus status, std::string reason) const
	{
		BotMovementEvent event;
		event.status = status;
		event.reason = std::move(reason);
		event.mapId = m_mapId;
		event.target = m_target;
		event.position = context.GetPosition();
		event.waypointIndex = m_nextWaypointIndex;
		event.pathPointCount = m_path.size();
		return event;
	}

	void BotMovementController::EmitUnreachable(BotContext& context, std::string reason)
	{
		SetStatus(BotMovementStatus::Unreachable, std::move(reason));
		TargetUnreachable(BuildEvent(context, m_status, m_lastReason));
		StopLowLevel(context, m_lastReason, false);
	}

	void BotMovementController::EmitReached(BotContext& context, std::string reason)
	{
		SetStatus(BotMovementStatus::Reached, std::move(reason));
		StopLowLevel(context, m_lastReason, false);
		TargetReached(BuildEvent(context, m_status, m_lastReason));
	}

	void BotMovementController::StopLowLevel(BotContext& context, std::string reason, const bool emitSignal)
	{
		if (m_runtime.isMoving || context.IsMoving())
		{
			MovementInfo movement = context.GetMovementInfo();
			movement.movementFlags &= ~movement_flags::Moving;
			movement.timestamp = context.GetServerTime();
			context.SendMovementUpdate(game::client_realm_packet::MoveStop, movement);
			context.UpdateMovementInfo(movement);
		}

		m_runtime.isMoving = false;
		m_runtime.velocity = Vector3::Zero;
		if (emitSignal)
		{
			MovementStopped(BuildEvent(context, m_status, std::move(reason)));
		}
	}

	void BotMovementController::SetStatus(const BotMovementStatus status, std::string reason)
	{
		m_status = status;
		m_lastReason = std::move(reason);
	}
}
