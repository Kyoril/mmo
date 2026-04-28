// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#pragma once

#include "../bot_action.h"
#include "../bot_context.h"
#include "../bot_follow_policy.h"
#include "../bot_movement_math.h"
#include "../bot_nav_service.h"
#include "../bot_unit.h"

#include "base/clock.h"
#include "game_protocol/game_protocol.h"
#include "log/default_log_levels.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string_view>

namespace mmo
{
	enum class FollowLeaderPreconditionFailure : uint8
	{
		None = 0,
		LeaderUnavailable,
		LeaderOutOfAwareness,
		InvalidAnchorPosition,
		NavUnavailable,
		MapUnavailable,
	};

	struct FollowLeaderControllerInput final
	{
		BotFollowAnchor anchor;
		BotFollowSample self;
		BotFollowPath path;
		BotFollowState state;
		GameTime now { 0 };
		FollowLeaderPreconditionFailure preconditionFailure { FollowLeaderPreconditionFailure::None };
	};

	struct FollowLeaderControllerOutput final
	{
		BotFollowDecision decision;
		bool shouldRequestPath { false };
	};

	class FollowLeaderController final
	{
	public:
		explicit FollowLeaderController(BotFollowConfig config = {});

		[[nodiscard]] const BotFollowConfig& GetConfig() const noexcept { return m_policy.GetConfig(); }
		[[nodiscard]] FollowLeaderControllerOutput Evaluate(const FollowLeaderControllerInput& input) const;

	private:
		BotFollowPolicy m_policy;
	};

	class FollowLeaderAction final : public BotAction
	{
	public:
		explicit FollowLeaderAction(BotFollowConfig config = {}, float moveSpeed = 7.0f)
			: m_controller(std::move(config))
			, m_moveSpeed(moveSpeed)
		{
		}

		std::string GetDescription() const override
		{
			return "Follow current party leader";
		}

		ActionResult Execute(BotContext& context) override
		{
			const GameTime now = context.GetServerTime();
			const FollowLeaderControllerInput input = BuildControllerInput(context, now);
			const FollowLeaderControllerOutput output = m_controller.Evaluate(input);
			ApplyDecision(context, input, output);
			return ActionResult::InProgress;
		}

		void OnAbort(BotContext& context) override
		{
			StopMovement(context);
			m_path.points.clear();
			m_state = {};
			m_lastHeartbeat = 0;
			m_lastDecisionKey.clear();
		}

		bool IsInterruptible() const override
		{
			return true;
		}

	private:
		[[nodiscard]] FollowLeaderControllerInput BuildControllerInput(BotContext& context, GameTime now) const
		{
			FollowLeaderControllerInput input;
			input.now = now;
			input.path = m_path;
			input.state = m_state;
			input.self.position = context.GetPosition();
			input.self.facing = context.GetMovementInfo().facing;
			input.self.timestamp = now;
			input.self.isMoving = context.IsMoving();
			input.self.isValid = context.HasAuthoritativeMovementInfo() || context.GetSelf() != nullptr;

			if (!context.IsInParty())
			{
				input.preconditionFailure = FollowLeaderPreconditionFailure::LeaderUnavailable;
				return input;
			}

			const uint64 leaderGuid = context.GetPartyLeaderGuid();
			if (leaderGuid == 0 || leaderGuid == context.GetSelectedCharacterGuid())
			{
				input.preconditionFailure = FollowLeaderPreconditionFailure::LeaderUnavailable;
				return input;
			}

			const BotUnit* leader = context.GetUnit(leaderGuid);
			if (!leader)
			{
				input.preconditionFailure = FollowLeaderPreconditionFailure::LeaderOutOfAwareness;
				return input;
			}

			input.anchor.kind = BotFollowAnchorKind::Leader;
			input.anchor.guid = leaderGuid;
			input.anchor.position = leader->GetPosition();
			input.anchor.hasPosition = true;
			input.anchor.facing = leader->GetFacing();
			input.anchor.hasFacing = true;
			input.anchor.label = leader->GetName();

			if (!IsFiniteVector(input.anchor.position))
			{
				input.preconditionFailure = FollowLeaderPreconditionFailure::InvalidAnchorPosition;
				return input;
			}

			if (!context.GetNavService() || !context.GetNavService()->IsReady())
			{
				input.preconditionFailure = FollowLeaderPreconditionFailure::NavUnavailable;
				return input;
			}

			if (!context.HasCurrentMapId() || context.GetCurrentMapId() == 0)
			{
				input.preconditionFailure = FollowLeaderPreconditionFailure::MapUnavailable;
			}

			return input;
		}

		void ApplyDecision(BotContext& context, const FollowLeaderControllerInput& input, const FollowLeaderControllerOutput& output)
		{
			switch (output.decision.type)
			{
			case BotFollowDecisionType::Hold:
				StopMovement(context);
				LogDecisionOnce("hold", output.decision.reason, BuildDecisionDetails(context, input, output.decision));
				return;

			case BotFollowDecisionType::Abort:
				StopMovement(context);
				m_path.points.clear();
				m_state.hasLastRepathAnchorPosition = false;
				LogDecisionOnce("abort", output.decision.reason, BuildDecisionDetails(context, input, output.decision));
				return;

			case BotFollowDecisionType::Stuck:
				StopMovement(context);
				m_path.points.clear();
				m_state.hasLastRepathAnchorPosition = false;
				LogDecisionOnce("stuck", output.decision.reason, BuildDecisionDetails(context, input, output.decision));
				return;

			case BotFollowDecisionType::Repath:
				RequestPath(context, input, output.decision);
				return;

			case BotFollowDecisionType::Advance:
				AdvanceAlongPath(context, input, output.decision);
				return;
			}
		}

		void RequestPath(BotContext& context, const FollowLeaderControllerInput& input, const BotFollowDecision& decision)
		{
			BotNavService* navService = context.GetNavService();
			if (!navService)
			{
				StopMovement(context);
				LogDecisionOnce("abort", "nav_unavailable", BuildDecisionDetails(context, input, decision));
				return;
			}

			LogDecisionOnce("repath", decision.reason, BuildDecisionDetails(context, input, decision));
			const BotPathResult result = navService->FindPath(context.GetCurrentMapId(), input.self.position, input.anchor.position);
			if (!result.success)
			{
				StopMovement(context);
				m_path.points.clear();
				m_state.hasLastRepathAnchorPosition = false;
				std::ostringstream details;
				details << BuildDecisionDetails(context, input, decision)
					<< " map_directory=" << (result.mapDirectory.empty() ? "unknown" : result.mapDirectory);
				LogDecisionOnce("abort", result.reason.empty() ? std::string_view("invalid_path") : std::string_view(result.reason), details.str());
				return;
			}

			m_path.points = result.points;
			m_state.hasLastRepathAnchorPosition = true;
			m_state.lastRepathAnchorPosition = input.anchor.position;
			m_state.lastRepathTime = input.now;
			m_state.hasLastDistanceToAnchor = true;
			m_state.lastDistanceToAnchor = decision.distanceToAnchor;
		}

		void AdvanceAlongPath(BotContext& context, const FollowLeaderControllerInput& input, const BotFollowDecision& decision)
		{
			if (!decision.hasSteeringTarget)
			{
				LogDecisionOnce("abort", "invalid_steering_target", BuildDecisionDetails(context, input, decision));
				StopMovement(context);
				return;
			}

			MovementInfo movement = context.GetMovementInfo();
			if (!m_state.isMoving && (movement.movementFlags & movement_flags::Falling))
			{
				context.SendLandedPacket();
				movement = context.GetMovementInfo();
			}

			if (decision.hasDesiredFacing)
			{
				movement.facing = decision.desiredFacing;
			}

			if (!m_state.isMoving)
			{
				movement.movementFlags |= movement_flags::Forward;
				movement.timestamp = input.now;
				context.SendMovementUpdate(game::client_realm_packet::MoveStartForward, movement);
				m_state.isMoving = true;
				m_lastHeartbeat = input.now;
				m_state.hasLastDistanceToAnchor = true;
				m_state.lastDistanceToAnchor = decision.distanceToAnchor;
				return;
			}

			if (input.now - m_lastHeartbeat < kHeartbeatIntervalMs)
			{
				return;
			}

			const Vector3 currentPosition = movement.position;
			Vector3 delta = decision.steeringTarget - currentPosition;
			delta.y = 0.0f;
			const float distance = delta.GetLength();
			const float elapsedSeconds = static_cast<float>(input.now - m_lastHeartbeat) / 1000.0f;
			if (distance > 0.001f && movement.IsChangingPosition())
			{
				const Vector3 direction = delta / distance;
				const float stepDistance = std::min(m_moveSpeed * elapsedSeconds, distance);
				movement.position = currentPosition + (direction * stepDistance);
				if (stepDistance > 0.0f)
				{
					m_state.hasLastProgressPosition = true;
					m_state.lastProgressPosition = movement.position;
					m_state.lastProgressTime = input.now;
				}
			}

			movement.timestamp = input.now;
			context.SendMovementUpdate(game::client_realm_packet::MoveHeartBeat, movement);
			m_lastHeartbeat = input.now;
			m_state.isMoving = true;
			m_state.hasLastDistanceToAnchor = true;
			m_state.lastDistanceToAnchor = decision.distanceToAnchor;
		}

		void StopMovement(BotContext& context)
		{
			if (!m_state.isMoving && !context.IsMoving())
			{
				return;
			}

			MovementInfo movement = context.GetMovementInfo();
			movement.movementFlags &= ~movement_flags::Moving;
			movement.timestamp = context.GetServerTime();
			context.SendMovementUpdate(game::client_realm_packet::MoveStop, movement);
			m_state.isMoving = false;
		}

		[[nodiscard]] std::string BuildDecisionDetails(BotContext& context, const FollowLeaderControllerInput& input, const BotFollowDecision& decision) const
		{
			std::ostringstream details;
			details << "leader_guid=" << input.anchor.guid
				<< " map_id=" << context.GetCurrentMapId()
				<< " distance=" << decision.distanceToAnchor
				<< " moving=" << (m_state.isMoving ? 1 : 0);
			if (input.anchor.hasPosition)
			{
				details << " anchor_x=" << input.anchor.position.x
					<< " anchor_y=" << input.anchor.position.y
					<< " anchor_z=" << input.anchor.position.z;
			}
			if (decision.hasSteeringTarget)
			{
				details << " steer_x=" << decision.steeringTarget.x
					<< " steer_y=" << decision.steeringTarget.y
					<< " steer_z=" << decision.steeringTarget.z
					<< " waypoint_index=" << decision.steeringWaypointIndex;
			}
			details << " path_points=" << m_path.points.size();
			return details.str();
		}

		void LogDecisionOnce(const std::string_view decision, const std::string_view reason, const std::string& details)
		{
			std::ostringstream keyBuilder;
			keyBuilder << decision << '|' << reason << '|' << details;
			const std::string key = keyBuilder.str();
			if (m_lastDecisionKey == key)
			{
				return;
			}

			m_lastDecisionKey = key;
			if (decision == "abort")
			{
				ELOG("movement decision=" << decision << " reason=" << reason << ' ' << details);
			}
			else
			{
				ILOG("movement decision=" << decision << " reason=" << reason << ' ' << details);
			}
		}

	private:
		static constexpr GameTime kHeartbeatIntervalMs { 500 };

		FollowLeaderController m_controller;
		BotFollowState m_state;
		BotFollowPath m_path;
		GameTime m_lastHeartbeat { 0 };
		float m_moveSpeed { 7.0f };
		std::string m_lastDecisionKey;
	};
}
