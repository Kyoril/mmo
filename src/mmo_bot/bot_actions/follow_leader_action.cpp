// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "follow_leader_action.h"

namespace mmo
{
	namespace
	{
		const char* ToReason(const FollowLeaderPreconditionFailure failure)
		{
			switch (failure)
			{
			case FollowLeaderPreconditionFailure::LeaderUnavailable:
				return "leader_unavailable";
			case FollowLeaderPreconditionFailure::LeaderOutOfAwareness:
				return "leader_out_of_awareness";
			case FollowLeaderPreconditionFailure::InvalidAnchorPosition:
				return "invalid_anchor_position";
			case FollowLeaderPreconditionFailure::NavUnavailable:
				return "nav_unavailable";
			case FollowLeaderPreconditionFailure::MapUnavailable:
				return "map_unresolved";
			case FollowLeaderPreconditionFailure::None:
			default:
				return "";
			}
		}
	}

	FollowLeaderController::FollowLeaderController(BotFollowConfig config)
		: m_policy(std::move(config))
	{
	}

	FollowLeaderControllerOutput FollowLeaderController::Evaluate(const FollowLeaderControllerInput& input) const
	{
		FollowLeaderControllerOutput output;

		if (input.preconditionFailure != FollowLeaderPreconditionFailure::None)
		{
			output.decision.type =
				(input.preconditionFailure == FollowLeaderPreconditionFailure::NavUnavailable
					|| input.preconditionFailure == FollowLeaderPreconditionFailure::MapUnavailable)
				? BotFollowDecisionType::Abort
				: BotFollowDecisionType::Hold;
			output.decision.reason = ToReason(input.preconditionFailure);
			output.decision.shouldStop = true;
			return output;
		}

		BotFollowPolicyInput policyInput;
		policyInput.anchor = input.anchor;
		policyInput.self = input.self;
		policyInput.path = input.path;
		policyInput.state = input.state;
		policyInput.now = input.now;
		output.decision = m_policy.Evaluate(policyInput);
		output.shouldRequestPath = output.decision.type == BotFollowDecisionType::Repath && output.decision.shouldRepath;
		return output;
	}
}
