// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "bot_follow_policy.h"

#include "bot_movement_math.h"

namespace
{
	constexpr std::size_t kNoWaypoint = static_cast<std::size_t>(-1);
}

namespace mmo
{
	BotFollowPolicy::BotFollowPolicy(BotFollowConfig config)
		: m_config(std::move(config))
	{
		if (m_config.leaveDistance < m_config.holdDistance)
		{
			m_config.leaveDistance = m_config.holdDistance;
		}
	}

	BotFollowDecision BotFollowPolicy::Evaluate(const BotFollowPolicyInput& input) const
	{
		BotFollowDecision decision;
		decision.distanceToAnchor = PlanarDistance(input.self.position, input.anchor.position);
		decision.shouldStop = true;

		if (!input.anchor.hasPosition || !IsFiniteVector(input.anchor.position))
		{
			decision.reason = "missing_anchor";
			return decision;
		}

		if (!input.self.isValid || !IsFiniteVector(input.self.position))
		{
			decision.reason = "missing_self";
			return decision;
		}

		decision.distanceToAnchor = PlanarDistance(input.self.position, input.anchor.position);
		if (input.anchor.hasFacing)
		{
			decision.desiredFacing = NormalizeFacing(input.anchor.facing);
			decision.hasDesiredFacing = true;
		}
		else if (decision.distanceToAnchor > 1e-3f)
		{
			decision.desiredFacing = ComputeFacingTo(input.self.position, input.anchor.position, input.self.facing);
			decision.hasDesiredFacing = true;
		}

		const bool insideHoldBand = decision.distanceToAnchor <= m_config.holdDistance;
		const bool shouldRemainStopped = !input.state.isMoving && decision.distanceToAnchor <= m_config.leaveDistance;
		if (insideHoldBand || shouldRemainStopped)
		{
			decision.type = BotFollowDecisionType::Hold;
			decision.reason = insideHoldBand ? "inside_hold_band" : "inside_leave_band";
			return decision;
		}

		if (input.state.isMoving && input.state.hasLastProgressPosition)
		{
			const GameTime elapsed = input.now >= input.state.lastProgressTime ? (input.now - input.state.lastProgressTime) : 0;
			if (elapsed >= m_config.nonProgressTimeoutMs)
			{
				const float progressDistance = PlanarDistance(input.state.lastProgressPosition, input.self.position);
				if (progressDistance < m_config.progressDistanceEpsilon)
				{
					decision.type = BotFollowDecisionType::Stuck;
					decision.reason = "non_progress";
					return decision;
				}
			}
		}

		if (!HasUsablePath(input.path))
		{
			decision.type = BotFollowDecisionType::Repath;
			decision.reason = "malformed_path";
			decision.shouldRepath = true;
			return decision;
		}

		if (const char* repathReason = DetermineRepathReason(input))
		{
			decision.type = BotFollowDecisionType::Repath;
			decision.reason = repathReason;
			decision.shouldRepath = true;
			return decision;
		}

		const std::size_t waypointIndex = FindSteeringWaypointIndex(input.self, input.path);
		if (waypointIndex == kNoWaypoint)
		{
			decision.type = BotFollowDecisionType::Repath;
			decision.reason = "path_exhausted";
			decision.shouldRepath = true;
			return decision;
		}

		decision.type = BotFollowDecisionType::Advance;
		decision.reason = "follow_path";
		decision.shouldStop = false;
		decision.steeringWaypointIndex = waypointIndex;
		decision.steeringTarget = ResolveSteeringTarget(input.path, waypointIndex);
		decision.hasSteeringTarget = true;
		decision.desiredFacing = ComputeFacingTo(input.self.position, decision.steeringTarget, input.self.facing);
		decision.hasDesiredFacing = true;
		return decision;
	}

	bool BotFollowPolicy::HasUsablePath(const BotFollowPath& path) const
	{
		if (path.points.size() < 2)
		{
			return false;
		}

		for (std::size_t i = 1; i < path.points.size(); ++i)
		{
			if (!IsFiniteVector(path.points[i - 1]) || !IsFiniteVector(path.points[i]))
			{
				return false;
			}

			if (!IsDegenerateSegment(path.points[i - 1], path.points[i]))
			{
				return true;
			}
		}

		return false;
	}

	std::size_t BotFollowPolicy::FindSteeringWaypointIndex(const BotFollowSample& self, const BotFollowPath& path) const
	{
		for (std::size_t i = 0; i < path.points.size(); ++i)
		{
			if (PlanarDistance(self.position, path.points[i]) > m_config.waypointAcceptanceRadius)
			{
				return i;
			}
		}

		return kNoWaypoint;
	}

	Vector3 BotFollowPolicy::ResolveSteeringTarget(const BotFollowPath& path, const std::size_t waypointIndex) const
	{
		return ComputeSmoothedPathTarget(
			path.points,
			waypointIndex,
			m_config.turnSmoothingThresholdRadians,
			m_config.turnSmoothingDistance);
	}

	const char* BotFollowPolicy::DetermineRepathReason(const BotFollowPolicyInput& input) const
	{
		if (!input.state.hasLastRepathAnchorPosition)
		{
			return "missing_repath_context";
		}

		const float anchorDrift = PlanarDistance(input.state.lastRepathAnchorPosition, input.anchor.position);
		if (anchorDrift >= m_config.repathAnchorDriftDistance)
		{
			return "anchor_drift";
		}

		const GameTime elapsed = input.now >= input.state.lastRepathTime ? (input.now - input.state.lastRepathTime) : 0;
		if (elapsed >= m_config.repathRefreshMs)
		{
			return "periodic_refresh";
		}

		return nullptr;
	}
}
