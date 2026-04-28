// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "companion_follow_action.h"

#include "../bot_movement_math.h"
#include "../bot_unit.h"

#include "game_protocol/game_protocol.h"
#include "log/default_log_levels.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_set>

namespace
{
	const char* FollowDecisionTypeToString(const mmo::BotFollowDecisionType type)
	{
		switch (type)
		{
		case mmo::BotFollowDecisionType::Advance:
			return "advance";
		case mmo::BotFollowDecisionType::Repath:
			return "repath";
		case mmo::BotFollowDecisionType::Abort:
			return "abort";
		case mmo::BotFollowDecisionType::Stuck:
			return "stuck";
		case mmo::BotFollowDecisionType::Hold:
		default:
			return "hold";
		}
	}

	const char* AnchorKindToString(const mmo::BotFollowAnchorKind kind)
	{
		switch (kind)
		{
		case mmo::BotFollowAnchorKind::Leader:
			return "leader";
		case mmo::BotFollowAnchorKind::Role:
			return "role";
		case mmo::BotFollowAnchorKind::Custom:
		default:
			return "custom";
		}
	}

	mmo::BotCompanionRole DetermineSelfRole(const uint8 characterClass)
	{
		switch (characterClass)
		{
		case 1: // Warrior
		case 2: // Paladin
		case 11: // Druid
			return mmo::BotCompanionRole::Tank;
		case 5: // Priest
			return mmo::BotCompanionRole::Healer;
		case 3: // Hunter
		case 8: // Mage
		case 9: // Warlock
			return mmo::BotCompanionRole::RangedDps;
		case 4: // Rogue
		case 6: // Death Knight
			return mmo::BotCompanionRole::MeleeDps;
		case 7: // Shaman
		default:
			return mmo::BotCompanionRole::None;
		}
	}

	mmo::BotCompanionRole DetermineDesiredCombatAnchorRole(const mmo::BotCompanionRole selfRole)
	{
		switch (selfRole)
		{
		case mmo::BotCompanionRole::Tank:
			return mmo::BotCompanionRole::None;
		case mmo::BotCompanionRole::Healer:
		case mmo::BotCompanionRole::MeleeDps:
		case mmo::BotCompanionRole::RangedDps:
			return mmo::BotCompanionRole::Tank;
		case mmo::BotCompanionRole::None:
		default:
			return mmo::BotCompanionRole::Tank;
		}
	}

	mmo::BotCompanionMemberSnapshot BuildMemberSnapshot(
		mmo::BotContext& context,
		const uint64 guid,
		const std::string& fallbackLabel,
		const uint64 selfGuid,
		const uint64 leaderGuid,
		const mmo::BotCompanionRole selfRole)
	{
		mmo::BotCompanionMemberSnapshot member;
		member.guid = guid;
		member.isSelf = guid == selfGuid;
		member.isLeader = guid == leaderGuid;
		member.isBot = member.isSelf;
		member.role = member.isSelf
			? selfRole
			: (member.isLeader ? mmo::BotCompanionRole::Tank : mmo::BotCompanionRole::None);
		member.label = fallbackLabel;

		if (const mmo::BotUnit* unit = context.GetUnit(guid))
		{
			member.position = unit->GetPosition();
			member.hasPosition = true;
			member.facing = unit->GetFacing();
			member.hasFacing = true;
			member.isAlive = unit->IsAlive();
			member.isAware = true;
			member.isInCombat = unit->IsInCombat();
			member.targetGuid = unit->GetTargetGuid();
			if (member.label.empty())
			{
				member.label = unit->GetName();
			}
		}
		else
		{
			member.isAlive = true;
			member.isAware = false;
			member.isInCombat = false;
		}

		if (member.label.empty())
		{
			if (member.isSelf)
			{
				member.label = "self";
			}
			else if (member.isLeader)
			{
				member.label = "leader";
			}
		}

		return member;
	}
}

namespace mmo
{
	CompanionFollowAction::CompanionFollowAction(BotFollowConfig config, const float moveSpeed)
		: m_controller(std::move(config))
		, m_moveSpeed(moveSpeed)
	{
	}

	std::string CompanionFollowAction::GetDescription() const
	{
		return "Follow the party leader out of combat and role-aware anchors in combat";
	}

	ActionResult CompanionFollowAction::Execute(BotContext& context)
	{
		const GameTime now = context.GetServerTime();
		const CompanionFollowControllerInput input = BuildControllerInput(context, now);
		const CompanionFollowControllerOutput output = m_controller.Evaluate(input);
		const std::string details = BuildDecisionDetails(context, input, output);
		LogCompanionModeOnce(output, details);
		LogAnchorDecisionOnce(output, details);
		ApplyDecision(context, input, output);
		UpdatePreviousCompanionState(output);
		return ActionResult::InProgress;
	}

	void CompanionFollowAction::OnAbort(BotContext& context)
	{
		StopMovement(context);
		m_path.points.clear();
		m_state = {};
		m_previousCompanionState = {};
		m_lastHeartbeat = 0;
		m_lastModeLogKey.clear();
		m_lastAnchorLogKey.clear();
	}

	bool CompanionFollowAction::IsInterruptible() const
	{
		return true;
	}

	CompanionFollowControllerInput CompanionFollowAction::BuildControllerInput(BotContext& context, const GameTime now) const
	{
		CompanionFollowControllerInput input;
		input.now = now;
		input.path = m_path;
		input.state = m_state;
		input.self.position = context.GetPosition();
		input.self.facing = context.GetMovementInfo().facing;
		input.self.timestamp = now;
		input.self.isMoving = context.IsMoving();
		input.self.isValid = context.HasAuthoritativeMovementInfo() || context.GetSelf() != nullptr;

		input.snapshot.selfGuid = context.GetSelectedCharacterGuid();
		input.snapshot.leaderGuid = context.GetPartyLeaderGuid();
		const BotCompanionRole selfRole = DetermineSelfRole(context.GetConfig().characterClass);
		input.snapshot.desiredCombatRole = DetermineDesiredCombatAnchorRole(selfRole);
		input.snapshot.previousState = m_previousCompanionState;

		const BotUnit* selfUnit = context.GetSelf();
		input.snapshot.selfInCombat = selfUnit ? selfUnit->IsInCombat() : false;

		std::unordered_set<uint64> seenGuids;
		const uint32 partyMemberCount = context.GetPartyMemberCount();
		input.snapshot.partyMembers.reserve(partyMemberCount + 2);
		for (uint32 index = 0; index < partyMemberCount; ++index)
		{
			const uint64 guid = context.GetPartyMemberGuid(index);
			if (guid == 0 || !seenGuids.insert(guid).second)
			{
				continue;
			}

			input.snapshot.partyMembers.push_back(BuildMemberSnapshot(
				context,
				guid,
				context.GetPartyMemberName(index),
				input.snapshot.selfGuid,
				input.snapshot.leaderGuid,
				selfRole));
		}

		if (input.snapshot.selfGuid != 0 && seenGuids.find(input.snapshot.selfGuid) == seenGuids.end())
		{
			input.snapshot.partyMembers.push_back(BuildMemberSnapshot(
				context,
				input.snapshot.selfGuid,
				selfUnit ? selfUnit->GetName() : std::string {},
				input.snapshot.selfGuid,
				input.snapshot.leaderGuid,
				selfRole));
			seenGuids.insert(input.snapshot.selfGuid);
		}

		if (input.snapshot.leaderGuid != 0 && seenGuids.find(input.snapshot.leaderGuid) == seenGuids.end())
		{
			const BotUnit* leaderUnit = context.GetUnit(input.snapshot.leaderGuid);
			input.snapshot.partyMembers.push_back(BuildMemberSnapshot(
				context,
				input.snapshot.leaderGuid,
				leaderUnit ? leaderUnit->GetName() : std::string("leader"),
				input.snapshot.selfGuid,
				input.snapshot.leaderGuid,
				selfRole));
		}

		for (const BotCompanionMemberSnapshot& member : input.snapshot.partyMembers)
		{
			if (member.isInCombat)
			{
				input.snapshot.partyInCombat = true;
				break;
			}
		}

		if (!input.self.isValid)
		{
			input.preconditionFailure = CompanionFollowPreconditionFailure::SelfUnavailable;
			return input;
		}

		if (!IsFiniteVector(input.self.position))
		{
			input.preconditionFailure = CompanionFollowPreconditionFailure::InvalidSelfPosition;
			return input;
		}

		if (!context.GetNavService() || !context.GetNavService()->IsReady())
		{
			input.preconditionFailure = CompanionFollowPreconditionFailure::NavUnavailable;
			return input;
		}

		if (!context.HasCurrentMapId() || context.GetCurrentMapId() == 0)
		{
			input.preconditionFailure = CompanionFollowPreconditionFailure::MapUnavailable;
		}

		return input;
	}

	void CompanionFollowAction::ApplyDecision(BotContext& context, const CompanionFollowControllerInput& input, const CompanionFollowControllerOutput& output)
	{
		switch (output.followDecision.type)
		{
		case BotFollowDecisionType::Hold:
			StopMovement(context);
			return;

		case BotFollowDecisionType::Abort:
			StopMovement(context);
			m_path.points.clear();
			m_state.hasLastRepathAnchorPosition = false;
			return;

		case BotFollowDecisionType::Stuck:
			StopMovement(context);
			m_path.points.clear();
			m_state.hasLastRepathAnchorPosition = false;
			return;

		case BotFollowDecisionType::Repath:
			RequestPath(context, input, output);
			return;

		case BotFollowDecisionType::Advance:
			AdvanceAlongPath(context, input, output);
			return;
		}
	}

	void CompanionFollowAction::RequestPath(BotContext& context, const CompanionFollowControllerInput& input, const CompanionFollowControllerOutput& output)
	{
		BotNavService* navService = context.GetNavService();
		if (!navService)
		{
			StopMovement(context);
			m_path.points.clear();
			m_state.hasLastRepathAnchorPosition = false;
			return;
		}

		const BotPathResult result = navService->FindPath(context.GetCurrentMapId(), input.self.position, output.companion.anchor.position);
		if (!result.success)
		{
			StopMovement(context);
			m_path.points.clear();
			m_state.hasLastRepathAnchorPosition = false;
			return;
		}

		m_path.points = result.points;
		m_state.hasLastRepathAnchorPosition = true;
		m_state.lastRepathAnchorPosition = output.companion.anchor.position;
		m_state.lastRepathTime = input.now;
		m_state.hasLastDistanceToAnchor = true;
		m_state.lastDistanceToAnchor = output.followDecision.distanceToAnchor;
	}

	void CompanionFollowAction::AdvanceAlongPath(BotContext& context, const CompanionFollowControllerInput& input, const CompanionFollowControllerOutput& output)
	{
		if (!output.followDecision.hasSteeringTarget)
		{
			StopMovement(context);
			return;
		}

		MovementInfo movement = context.GetMovementInfo();
		if (!m_state.isMoving && (movement.movementFlags & movement_flags::Falling))
		{
			context.SendLandedPacket();
			movement = context.GetMovementInfo();
		}

		if (output.followDecision.hasDesiredFacing)
		{
			movement.facing = output.followDecision.desiredFacing;
		}

		if (!m_state.isMoving)
		{
			movement.movementFlags |= movement_flags::Forward;
			movement.timestamp = input.now;
			context.SendMovementUpdate(game::client_realm_packet::MoveStartForward, movement);
			m_state.isMoving = true;
			m_lastHeartbeat = input.now;
			m_state.hasLastDistanceToAnchor = true;
			m_state.lastDistanceToAnchor = output.followDecision.distanceToAnchor;
			return;
		}

		if (input.now - m_lastHeartbeat < kHeartbeatIntervalMs)
		{
			return;
		}

		const Vector3 currentPosition = movement.position;
		Vector3 delta = output.followDecision.steeringTarget - currentPosition;
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
		m_state.lastDistanceToAnchor = output.followDecision.distanceToAnchor;
	}

	void CompanionFollowAction::StopMovement(BotContext& context)
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

	std::string CompanionFollowAction::BuildDecisionDetails(BotContext& context, const CompanionFollowControllerInput& input, const CompanionFollowControllerOutput& output) const
	{
		std::ostringstream details;
		details << "mode_reason=" << output.companion.modeReason
			<< " anchor_reason=" << output.companion.anchorReason
			<< " follow_decision=" << FollowDecisionTypeToString(output.followDecision.type)
			<< " follow_reason=" << output.followDecision.reason
			<< " desired_role=" << mmo::ToString(input.snapshot.desiredCombatRole)
			<< " leader_guid=" << input.snapshot.leaderGuid
			<< " map_id=" << context.GetCurrentMapId()
			<< " path_points=" << m_path.points.size();
		if (output.companion.hasAnchor)
		{
			details << " anchor_kind=" << AnchorKindToString(output.companion.anchor.kind)
				<< " anchor_guid=" << output.companion.anchor.guid;
			if (output.companion.anchor.hasPosition)
			{
				details << " anchor_x=" << output.companion.anchor.position.x
					<< " anchor_y=" << output.companion.anchor.position.y
					<< " anchor_z=" << output.companion.anchor.position.z;
			}
		}
		else
		{
			details << " anchor_kind=none anchor_guid=0";
		}
		if (output.followDecision.hasSteeringTarget)
		{
			details << " steer_x=" << output.followDecision.steeringTarget.x
				<< " steer_y=" << output.followDecision.steeringTarget.y
				<< " steer_z=" << output.followDecision.steeringTarget.z
				<< " waypoint_index=" << output.followDecision.steeringWaypointIndex;
		}
		return details.str();
	}

	void CompanionFollowAction::LogCompanionModeOnce(const CompanionFollowControllerOutput& output, const std::string& details)
	{
		std::ostringstream keyBuilder;
		keyBuilder << mmo::ToString(output.companion.mode) << '|' << output.companion.modeReason;
		const std::string key = keyBuilder.str();
		if (m_lastModeLogKey == key)
		{
			return;
		}

		m_lastModeLogKey = key;
		ILOG("companion mode=" << mmo::ToString(output.companion.mode) << " reason=" << output.companion.modeReason << ' ' << details);
	}

	void CompanionFollowAction::LogAnchorDecisionOnce(const CompanionFollowControllerOutput& output, const std::string& details)
	{
		std::ostringstream keyBuilder;
		keyBuilder << output.companion.anchorReason << '|'
			<< FollowDecisionTypeToString(output.followDecision.type) << '|'
			<< output.followDecision.reason << '|'
			<< (output.companion.hasAnchor ? output.companion.anchor.guid : 0) << '|'
			<< AnchorKindToString(output.companion.anchor.kind);
		const std::string key = keyBuilder.str();
		if (m_lastAnchorLogKey == key)
		{
			return;
		}

		m_lastAnchorLogKey = key;
		if (output.followDecision.type == BotFollowDecisionType::Abort || output.followDecision.type == BotFollowDecisionType::Stuck)
		{
			ELOG("anchor decision=" << output.companion.anchorReason << " follow=" << FollowDecisionTypeToString(output.followDecision.type) << " reason=" << output.followDecision.reason << ' ' << details);
		}
		else
		{
			ILOG("anchor decision=" << output.companion.anchorReason << " follow=" << FollowDecisionTypeToString(output.followDecision.type) << " reason=" << output.followDecision.reason << ' ' << details);
		}
	}

	void CompanionFollowAction::UpdatePreviousCompanionState(const CompanionFollowControllerOutput& output)
	{
		m_previousCompanionState.mode = output.companion.mode;
		m_previousCompanionState.anchorGuid = output.companion.hasAnchor ? output.companion.anchor.guid : 0;
		m_previousCompanionState.anchorKind = output.companion.hasAnchor ? output.companion.anchor.kind : BotFollowAnchorKind::Custom;
	}
}
