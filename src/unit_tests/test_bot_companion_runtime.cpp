// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "catch.hpp"

#include "mmo_bot/bot_actions/companion_follow_action.h"

namespace mmo
{
	namespace
	{
		BotCompanionSnapshot MakeSnapshot()
		{
			BotCompanionSnapshot snapshot;
			snapshot.selfGuid = 10;
			snapshot.leaderGuid = 20;
			snapshot.desiredCombatRole = BotCompanionRole::Tank;

			BotCompanionMemberSnapshot self;
			self.guid = snapshot.selfGuid;
			self.role = BotCompanionRole::Healer;
			self.position = Vector3(0.0f, 0.0f, 0.0f);
			self.hasPosition = true;
			self.facing = Radian(0.0f);
			self.hasFacing = true;
			self.isSelf = true;
			self.isBot = true;
			self.isAlive = true;
			self.isAware = true;
			self.label = "bot";
			snapshot.partyMembers.push_back(self);

			BotCompanionMemberSnapshot leader;
			leader.guid = snapshot.leaderGuid;
			leader.role = BotCompanionRole::Tank;
			leader.position = Vector3(8.0f, 0.0f, 0.0f);
			leader.hasPosition = true;
			leader.facing = Radian(1.0f);
			leader.hasFacing = true;
			leader.isLeader = true;
			leader.isAlive = true;
			leader.isAware = true;
			leader.label = "leader";
			snapshot.partyMembers.push_back(leader);

			BotCompanionMemberSnapshot dps;
			dps.guid = 30;
			dps.role = BotCompanionRole::RangedDps;
			dps.position = Vector3(12.0f, 0.0f, 4.0f);
			dps.hasPosition = true;
			dps.isAlive = true;
			dps.isAware = true;
			dps.label = "mage";
			snapshot.partyMembers.push_back(dps);

			return snapshot;
		}

		CompanionFollowControllerInput MakeInput()
		{
			CompanionFollowControllerInput input;
			input.now = 1000;
			input.snapshot = MakeSnapshot();
			input.self.position = Vector3::Zero;
			input.self.facing = Radian(0.0f);
			input.self.timestamp = input.now;
			input.self.isValid = true;
			input.path.points = {
				Vector3(0.0f, 0.0f, 0.0f),
				Vector3(8.0f, 0.0f, 0.0f),
				Vector3(8.0f, 0.0f, 2.0f),
			};
			input.state.hasLastRepathAnchorPosition = true;
			input.state.lastRepathAnchorPosition = Vector3(8.0f, 0.0f, 0.0f);
			input.state.lastRepathTime = 900;
			return input;
		}
	}

	TEST_CASE("companion runtime follows the live leader while traveling", "[bot-companion][runtime]")
	{
		CompanionFollowController controller;
		CompanionFollowControllerInput input = MakeInput();
		input.snapshot.partyInCombat = false;
		input.snapshot.selfInCombat = false;

		const CompanionFollowControllerOutput output = controller.Evaluate(input);
		REQUIRE(output.companion.mode == BotCompanionMode::LeaderFollow);
		CHECK(output.companion.modeReason == "party_out_of_combat");
		CHECK(output.companion.hasAnchor);
		CHECK(output.companion.anchor.guid == input.snapshot.leaderGuid);
		CHECK(output.followDecision.type == BotFollowDecisionType::Advance);
		CHECK(output.followDecision.reason == "follow_path");
		CHECK_FALSE(output.shouldRequestPath);
	}

	TEST_CASE("companion runtime re-reads live leader changes instead of sticking to stale callbacks", "[bot-companion][runtime]")
	{
		CompanionFollowController controller;
		CompanionFollowControllerInput input = MakeInput();
		input.snapshot.partyInCombat = false;
		input.snapshot.previousState.mode = BotCompanionMode::LeaderFollow;
		input.snapshot.previousState.anchorGuid = 20;
		input.snapshot.previousState.anchorKind = BotFollowAnchorKind::Leader;
		input.snapshot.leaderGuid = 30;
		input.snapshot.partyMembers[1].isLeader = false;
		input.snapshot.partyMembers[2].isLeader = true;
		input.snapshot.partyMembers[2].role = BotCompanionRole::Tank;

		const CompanionFollowControllerOutput output = controller.Evaluate(input);
		REQUIRE(output.companion.mode == BotCompanionMode::LeaderFollow);
		CHECK(output.companion.anchor.guid == 30);
		CHECK(output.followDecision.type == BotFollowDecisionType::Repath);
		CHECK(output.followDecision.reason == "anchor_drift");
		CHECK(output.shouldRequestPath);
	}

	TEST_CASE("companion runtime switches to role-aware combat anchoring", "[bot-companion][runtime]")
	{
		CompanionFollowController controller;
		CompanionFollowControllerInput input = MakeInput();
		input.snapshot.partyInCombat = true;
		input.snapshot.partyMembers[1].isInCombat = true;
		input.state.lastRepathAnchorPosition = input.snapshot.partyMembers[1].position;

		const CompanionFollowControllerOutput output = controller.Evaluate(input);
		REQUIRE(output.companion.mode == BotCompanionMode::CombatAnchor);
		CHECK(output.companion.anchor.kind == BotFollowAnchorKind::Role);
		CHECK(output.companion.anchor.guid == input.snapshot.leaderGuid);
		CHECK(output.companion.anchorReason == "role_anchor_tank");
		CHECK(output.followDecision.type == BotFollowDecisionType::Advance);
		CHECK(output.followDecision.reason == "follow_path");
		CHECK_FALSE(output.shouldRequestPath);
	}

	TEST_CASE("companion runtime regroups explicitly when a stale combat anchor disappears mid-follow", "[bot-companion][runtime]")
	{
		CompanionFollowController controller;
		CompanionFollowControllerInput input = MakeInput();
		input.snapshot.partyInCombat = true;
		input.snapshot.partyMembers[1].role = BotCompanionRole::Healer;
		input.snapshot.partyMembers[1].isInCombat = true;
		input.snapshot.previousState.mode = BotCompanionMode::CombatAnchor;
		input.snapshot.previousState.anchorGuid = 99;
		input.snapshot.previousState.anchorKind = BotFollowAnchorKind::Role;

		const CompanionFollowControllerOutput output = controller.Evaluate(input);
		REQUIRE(output.companion.mode == BotCompanionMode::Regroup);
		CHECK(output.companion.modeReason == "stale_combat_anchor");
		CHECK(output.companion.anchor.guid == input.snapshot.leaderGuid);
		CHECK(output.followDecision.type == BotFollowDecisionType::Advance);
		CHECK(output.followDecision.reason == "follow_path");
	}

	TEST_CASE("companion runtime holds conservatively when the leader guid becomes zero", "[bot-companion][runtime]")
	{
		CompanionFollowController controller;
		CompanionFollowControllerInput input = MakeInput();
		input.snapshot.partyInCombat = false;
		input.snapshot.leaderGuid = 0;
		input.snapshot.partyMembers[1].isLeader = false;

		const CompanionFollowControllerOutput output = controller.Evaluate(input);
		REQUIRE(output.companion.mode == BotCompanionMode::Hold);
		CHECK(output.companion.modeReason == "leader_guid_missing");
		CHECK_FALSE(output.companion.hasAnchor);
		CHECK(output.followDecision.type == BotFollowDecisionType::Hold);
		CHECK(output.followDecision.reason == "leader_guid_missing");
		CHECK_FALSE(output.shouldRequestPath);
	}

	TEST_CASE("companion runtime holds when the visible leader anchor is missing from awareness", "[bot-companion][runtime]")
	{
		CompanionFollowController controller;
		CompanionFollowControllerInput input = MakeInput();
		input.snapshot.partyInCombat = false;
		input.snapshot.partyMembers[1].isAware = false;
		input.snapshot.partyMembers[1].hasPosition = false;

		const CompanionFollowControllerOutput output = controller.Evaluate(input);
		REQUIRE(output.companion.mode == BotCompanionMode::Hold);
		CHECK(output.companion.modeReason == "leader_out_of_awareness");
		CHECK(output.followDecision.type == BotFollowDecisionType::Hold);
		CHECK(output.followDecision.reason == "leader_out_of_awareness");
	}

	TEST_CASE("companion runtime aborts explicitly when nav becomes unavailable", "[bot-companion][runtime]")
	{
		CompanionFollowController controller;
		CompanionFollowControllerInput input = MakeInput();
		input.snapshot.partyInCombat = true;
		input.snapshot.partyMembers[1].isInCombat = true;
		input.preconditionFailure = CompanionFollowPreconditionFailure::NavUnavailable;

		const CompanionFollowControllerOutput output = controller.Evaluate(input);
		REQUIRE(output.companion.mode == BotCompanionMode::CombatAnchor);
		CHECK(output.followDecision.type == BotFollowDecisionType::Abort);
		CHECK(output.followDecision.reason == "nav_unavailable");
		CHECK_FALSE(output.shouldRequestPath);
	}

	TEST_CASE("companion runtime aborts explicitly when the map is unresolved", "[bot-companion][runtime]")
	{
		CompanionFollowController controller;
		CompanionFollowControllerInput input = MakeInput();
		input.snapshot.partyInCombat = false;
		input.preconditionFailure = CompanionFollowPreconditionFailure::MapUnavailable;

		const CompanionFollowControllerOutput output = controller.Evaluate(input);
		REQUIRE(output.companion.mode == BotCompanionMode::LeaderFollow);
		CHECK(output.followDecision.type == BotFollowDecisionType::Abort);
		CHECK(output.followDecision.reason == "map_unresolved");
		CHECK_FALSE(output.shouldRequestPath);
	}

	TEST_CASE("companion runtime rejects malformed self snapshots conservatively", "[bot-companion][runtime]")
	{
		CompanionFollowController controller;
		CompanionFollowControllerInput input = MakeInput();
		input.preconditionFailure = CompanionFollowPreconditionFailure::SelfUnavailable;

		const CompanionFollowControllerOutput output = controller.Evaluate(input);
		REQUIRE(output.followDecision.type == BotFollowDecisionType::Hold);
		CHECK(output.followDecision.reason == "self_unavailable");
		CHECK_FALSE(output.shouldRequestPath);
	}
}
