// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "catch.hpp"

#include "mmo_bot/bot_follow_policy.h"
#include "mmo_bot/bot_movement_math.h"

namespace mmo
{
	namespace
	{
		BotFollowPolicyInput MakeInput()
		{
			BotFollowPolicyInput input;
			input.now = 1000;
			input.anchor.kind = BotFollowAnchorKind::Leader;
			input.anchor.guid = 42;
			input.anchor.position = Vector3(5.0f, 0.0f, 0.0f);
			input.anchor.hasPosition = true;
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

	TEST_CASE("follow policy keeps stopped bots parked within the leave band", "[bot-follow][policy]")
	{
		BotFollowPolicy policy;
		BotFollowPolicyInput input = MakeInput();
		input.anchor.position = Vector3(3.0f, 0.0f, 0.0f);
		input.state.isMoving = false;

		const BotFollowDecision decision = policy.Evaluate(input);
		REQUIRE(decision.type == BotFollowDecisionType::Hold);
		CHECK(decision.reason == "inside_leave_band");
		CHECK(decision.shouldStop);
		CHECK_FALSE(decision.shouldRepath);
	}

	TEST_CASE("follow policy avoids mirror-step repaths when anchor drift stays below threshold", "[bot-follow][policy]")
	{
		BotFollowPolicy policy;
		BotFollowPolicyInput input = MakeInput();
		input.anchor.position = Vector3(6.6f, 0.0f, 0.0f);
		input.state.lastRepathAnchorPosition = Vector3(5.0f, 0.0f, 0.0f);
		input.state.lastRepathTime = 900;
		input.state.isMoving = true;

		const BotFollowDecision decision = policy.Evaluate(input);
		REQUIRE(decision.type == BotFollowDecisionType::Advance);
		CHECK(decision.reason == "follow_path");
		CHECK(decision.hasSteeringTarget);
		CHECK_FALSE(decision.shouldRepath);
	}

	TEST_CASE("follow policy repaths when anchor drift crosses the threshold", "[bot-follow][policy]")
	{
		BotFollowPolicy policy;
		BotFollowPolicyInput input = MakeInput();
		input.anchor.position = Vector3(7.0f, 0.0f, 0.0f);
		input.state.lastRepathAnchorPosition = Vector3(5.0f, 0.0f, 0.0f);
		input.state.lastRepathTime = 900;
		input.state.isMoving = true;

		const BotFollowDecision decision = policy.Evaluate(input);
		REQUIRE(decision.type == BotFollowDecisionType::Repath);
		CHECK(decision.reason == "anchor_drift");
		CHECK(decision.shouldRepath);
		CHECK(decision.shouldStop);
	}

	TEST_CASE("follow policy repaths on the periodic refresh cadence", "[bot-follow][policy]")
	{
		BotFollowPolicy policy;
		BotFollowPolicyInput input = MakeInput();
		input.now = 1500;
		input.state.lastRepathTime = 1000;
		input.state.lastRepathAnchorPosition = input.anchor.position;
		input.state.isMoving = true;

		const BotFollowDecision decision = policy.Evaluate(input);
		REQUIRE(decision.type == BotFollowDecisionType::Repath);
		CHECK(decision.reason == "periodic_refresh");
		CHECK(decision.shouldRepath);
	}

	TEST_CASE("follow policy reports explicit non-progress stalls", "[bot-follow][policy]")
	{
		BotFollowPolicy policy;
		BotFollowPolicyInput input = MakeInput();
		input.now = 3000;
		input.state.isMoving = true;
		input.state.hasLastProgressPosition = true;
		input.state.lastProgressPosition = input.self.position;
		input.state.lastProgressTime = 1000;
		input.state.lastRepathTime = 2900;

		const BotFollowDecision decision = policy.Evaluate(input);
		REQUIRE(decision.type == BotFollowDecisionType::Stuck);
		CHECK(decision.reason == "non_progress");
		CHECK(decision.shouldStop);
	}

	TEST_CASE("follow policy rejects malformed path samples explicitly", "[bot-follow][policy]")
	{
		BotFollowPolicy policy;
		BotFollowPolicyInput input = MakeInput();
		input.path.points = { Vector3(0.0f, 0.0f, 0.0f) };
		input.state.isMoving = true;

		const BotFollowDecision decision = policy.Evaluate(input);
		REQUIRE(decision.type == BotFollowDecisionType::Repath);
		CHECK(decision.reason == "malformed_path");
		CHECK(decision.shouldRepath);
	}

	TEST_CASE("follow policy holds conservatively when anchor data is missing", "[bot-follow][policy]")
	{
		BotFollowPolicy policy;
		BotFollowPolicyInput input = MakeInput();
		input.anchor.hasPosition = false;

		const BotFollowDecision decision = policy.Evaluate(input);
		REQUIRE(decision.type == BotFollowDecisionType::Hold);
		CHECK(decision.reason == "missing_anchor");
		CHECK(decision.shouldStop);
	}

	TEST_CASE("movement math keeps the bot facing convention stable", "[bot-follow][policy]")
	{
		CHECK(ComputeFacingTo(Vector3::Zero, Vector3(0.0f, 0.0f, 4.0f)).GetValueRadians() == Approx(0.0f).margin(0.0001f));
		CHECK(ComputeFacingTo(Vector3::Zero, Vector3(4.0f, 0.0f, 0.0f)).GetValueRadians() == Approx(Pi * 0.5f).margin(0.0001f));
		CHECK(ComputeFacingTo(Vector3::Zero, Vector3(-4.0f, 0.0f, 0.0f)).GetValueRadians() == Approx(-Pi * 0.5f).margin(0.0001f));
		CHECK(SmallestAngleDelta(Radian(Pi - 0.05f), Radian(-Pi + 0.05f)) == Approx(0.10f).margin(0.0001f));
	}

	TEST_CASE("movement math trims sharp turns but leaves shallow turns alone", "[bot-follow][policy]")
	{
		const std::vector<Vector3> sharpTurn = {
			Vector3(0.0f, 0.0f, 0.0f),
			Vector3(10.0f, 0.0f, 0.0f),
			Vector3(10.0f, 0.0f, 10.0f),
		};
		const Vector3 sharpTarget = ComputeSmoothedPathTarget(sharpTurn, 1, 0.524f, 0.5f);
		CHECK(sharpTarget.x < 10.0f);
		CHECK(sharpTarget.z == Approx(0.0f).margin(0.0001f));

		const std::vector<Vector3> shallowTurn = {
			Vector3(0.0f, 0.0f, 0.0f),
			Vector3(10.0f, 0.0f, 0.0f),
			Vector3(20.0f, 0.0f, 1.0f),
		};
		const Vector3 shallowTarget = ComputeSmoothedPathTarget(shallowTurn, 1, 0.524f, 0.5f);
		CHECK(shallowTarget.x == Approx(10.0f).margin(0.0001f));
		CHECK(shallowTarget.z == Approx(0.0f).margin(0.0001f));
	}
}
