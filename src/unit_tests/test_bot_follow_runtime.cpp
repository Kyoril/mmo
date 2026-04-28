// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "catch.hpp"
#include "mmo_bot/bot_actions/follow_leader_action.h"

namespace mmo
{
	namespace
	{
		FollowLeaderControllerInput MakeInput()
		{
			FollowLeaderControllerInput input;
			input.now = 1000;
			input.anchor.kind = BotFollowAnchorKind::Leader;
			input.anchor.guid = 7;
			input.anchor.position = Vector3(5.0f, 0.0f, 0.0f);
			input.anchor.hasPosition = true;
			input.anchor.facing = Radian(0.0f);
			input.anchor.hasFacing = true;
			input.self.position = Vector3::Zero;
			input.self.facing = Radian(0.0f);
			input.self.isValid = true;
			input.path.points = {
				Vector3(0.0f, 0.0f, 0.0f),
				Vector3(5.0f, 0.0f, 0.0f),
				Vector3(5.0f, 0.0f, 2.0f),
			};
			input.state.hasLastRepathAnchorPosition = true;
			input.state.lastRepathAnchorPosition = input.anchor.position;
			input.state.lastRepathTime = 900;
			return input;
		}
	}

	TEST_CASE("follow runtime keeps stopped bots parked near a settled leader", "[bot-follow][runtime]")
	{
		FollowLeaderController controller;
		FollowLeaderControllerInput input = MakeInput();
		input.anchor.position = Vector3(3.0f, 0.0f, 0.0f);
		input.state.isMoving = false;

		const FollowLeaderControllerOutput output = controller.Evaluate(input);
		REQUIRE(output.decision.type == BotFollowDecisionType::Hold);
		CHECK(output.decision.reason == "inside_leave_band");
		CHECK_FALSE(output.shouldRequestPath);
	}

	TEST_CASE("follow runtime avoids mirror-step repaths while the leader drifts within cadence thresholds", "[bot-follow][runtime]")
	{
		FollowLeaderController controller;
		FollowLeaderControllerInput input = MakeInput();
		input.anchor.position = Vector3(6.6f, 0.0f, 0.0f);
		input.state.isMoving = true;
		input.state.lastRepathAnchorPosition = Vector3(5.0f, 0.0f, 0.0f);
		input.state.lastRepathTime = 900;

		const FollowLeaderControllerOutput output = controller.Evaluate(input);
		REQUIRE(output.decision.type == BotFollowDecisionType::Advance);
		CHECK(output.decision.reason == "follow_path");
		CHECK_FALSE(output.shouldRequestPath);
	}

	TEST_CASE("follow runtime requests a bounded repath when the leader drifts beyond threshold", "[bot-follow][runtime]")
	{
		FollowLeaderController controller;
		FollowLeaderControllerInput input = MakeInput();
		input.anchor.position = Vector3(7.0f, 0.0f, 0.0f);
		input.state.isMoving = true;
		input.state.lastRepathAnchorPosition = Vector3(5.0f, 0.0f, 0.0f);
		input.state.lastRepathTime = 900;

		const FollowLeaderControllerOutput output = controller.Evaluate(input);
		REQUIRE(output.decision.type == BotFollowDecisionType::Repath);
		CHECK(output.decision.reason == "anchor_drift");
		CHECK(output.shouldRequestPath);
	}

	TEST_CASE("follow runtime reports leader awareness loss explicitly", "[bot-follow][runtime]")
	{
		FollowLeaderController controller;
		FollowLeaderControllerInput input = MakeInput();
		input.preconditionFailure = FollowLeaderPreconditionFailure::LeaderOutOfAwareness;

		const FollowLeaderControllerOutput output = controller.Evaluate(input);
		REQUIRE(output.decision.type == BotFollowDecisionType::Hold);
		CHECK(output.decision.reason == "leader_out_of_awareness");
		CHECK_FALSE(output.shouldRequestPath);
	}

	TEST_CASE("follow runtime aborts explicitly when nav is unavailable", "[bot-follow][runtime]")
	{
		FollowLeaderController controller;
		FollowLeaderControllerInput input = MakeInput();
		input.preconditionFailure = FollowLeaderPreconditionFailure::NavUnavailable;

		const FollowLeaderControllerOutput output = controller.Evaluate(input);
		REQUIRE(output.decision.type == BotFollowDecisionType::Abort);
		CHECK(output.decision.reason == "nav_unavailable");
		CHECK_FALSE(output.shouldRequestPath);
	}

	TEST_CASE("follow runtime surfaces non-progress windows as stuck decisions", "[bot-follow][runtime]")
	{
		FollowLeaderController controller;
		FollowLeaderControllerInput input = MakeInput();
		input.now = 3000;
		input.state.isMoving = true;
		input.state.hasLastProgressPosition = true;
		input.state.lastProgressPosition = input.self.position;
		input.state.lastProgressTime = 1000;
		input.state.lastRepathTime = 2900;

		const FollowLeaderControllerOutput output = controller.Evaluate(input);
		REQUIRE(output.decision.type == BotFollowDecisionType::Stuck);
		CHECK(output.decision.reason == "non_progress");
		CHECK_FALSE(output.shouldRequestPath);
	}
}
