// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "bot_movement_controller.h"

#include "bot_context.h"
#include "bot_movement_math.h"
#include "bot_nav_service.h"
#include "bot_unit.h"

#include "game/movement_type.h"
#include "game_protocol/game_protocol.h"
#include "log/default_log_levels.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>

namespace mmo
{
	namespace
	{
		Vector3 MoveTowards(const Vector3& current, const Vector3& target, const float maxDistance)
		{
			Vector3 delta = target - current;
			const float distance = delta.GetLength();
			if (distance <= 1e-5f || distance <= maxDistance)
			{
				return target;
			}

			return current + (delta / distance) * maxDistance;
		}

		Vector3 ComputePlanarStepTarget(const Vector3& current, const Vector3& target, const float stepDistance)
		{
			const float planarDistance = PlanarDistance(current, target);
			if (planarDistance <= 1e-5f || planarDistance <= stepDistance)
			{
				return target;
			}

			const float t = stepDistance / planarDistance;
			return Vector3(
				current.x + (target.x - current.x) * t,
				current.y + (target.y - current.y) * t,
				current.z + (target.z - current.z) * t);
		}

		std::size_t FindInitialWaypointIndex(const std::vector<Vector3>& path)
		{
			return path.size() > 1 ? 1u : 0u;
		}
	}

	BotLowLevelMovementOutput AdvanceBotLowLevelMovement(const BotLowLevelMovementInput& input)
	{
		BotLowLevelMovementOutput output;
		output.movement = input.movement;
		output.runtime = input.runtime;

		output.distanceToSteeringTarget = PlanarDistance(input.movement.position, input.steeringTarget);
		if (output.distanceToSteeringTarget <= input.acceptanceRadius)
		{
			output.runtime.velocity = Vector3::Zero;
			output.reachedSteeringTarget = true;
			return output;
		}

		const GameTime elapsedMs = input.now >= input.runtime.lastSimulationTime
			? input.now - input.runtime.lastSimulationTime
			: 0;
		if (elapsedMs == 0)
		{
			return output;
		}

		const float deltaSeconds = static_cast<float>(elapsedMs) / 1000.0f;
		const Vector3 direction = SafeNormalizePlanar(input.steeringTarget - input.movement.position);
		const Vector3 desiredVelocity = direction * std::max(0.0f, input.maxSpeed);
		const float maxVelocityDelta = std::max(0.0f, input.maxAcceleration) * deltaSeconds;
		output.runtime.velocity = MoveTowards(output.runtime.velocity, desiredVelocity, maxVelocityDelta);

		const float speed = FlattenToGround(output.runtime.velocity).GetLength();
		if (speed <= 1e-5f)
		{
			output.runtime.lastSimulationTime = input.now;
			return output;
		}

		const float maxStepDistance = speed * deltaSeconds;
		const Vector3 newPosition = ComputePlanarStepTarget(input.movement.position, input.steeringTarget, maxStepDistance);
		output.moved = PlanarDistanceSquared(input.movement.position, newPosition) > 1e-6f;
		output.movement.position = newPosition;
		output.movement.facing = ComputeFacingTo(input.movement.position, input.steeringTarget, input.movement.facing);
		output.movement.timestamp = input.now;
		output.runtime.lastSimulationTime = input.now;

		if (output.moved)
		{
			output.runtime.lastProgressPosition = output.movement.position;
			output.runtime.lastProgressTime = input.now;
			output.runtime.hasLastProgressPosition = true;
		}

		output.distanceToSteeringTarget = PlanarDistance(output.movement.position, input.steeringTarget);
		output.reachedSteeringTarget = output.distanceToSteeringTarget <= input.acceptanceRadius;
		return output;
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

		while (m_nextWaypointIndex < m_path.size()
			&& PlanarDistance(movement.position, m_path[m_nextWaypointIndex]) <= m_settings.waypointAcceptanceRadius)
		{
			++m_nextWaypointIndex;
			WaypointAdvanced(BuildEvent(context, m_status, "waypoint_reached"));
		}

		if (m_nextWaypointIndex >= m_path.size())
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
			SetStatus(BotMovementStatus::Stuck, "non_progress");
			TargetUnreachable(BuildEvent(context, m_status, m_lastReason));
			StopLowLevel(context, m_lastReason, true);
			return m_status;
		}

		const Vector3 steeringTarget = ResolveSteeringTarget();
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
		if (PlanarDistance(start, target) <= acceptanceRadius)
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

	Vector3 BotMovementController::ResolveSteeringTarget() const
	{
		if (m_path.empty())
		{
			return m_target;
		}

		return ComputeSmoothedPathTarget(
			m_path,
			m_nextWaypointIndex,
			m_settings.turnSmoothingThresholdRadians,
			m_settings.turnSmoothingDistance);
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
