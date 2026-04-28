// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "catch.hpp"

#include "mmo_bot/bot_companion_state_machine.h"

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
			dps.position = Vector3(10.0f, 0.0f, 4.0f);
			dps.hasPosition = true;
			dps.isAlive = true;
			dps.isAware = true;
			dps.label = "mage";
			snapshot.partyMembers.push_back(dps);

			return snapshot;
		}
	}

	TEST_CASE("companion state machine follows the party leader while traveling", "[bot-companion][state]")
	{
		BotCompanionStateMachine machine;
		BotCompanionSnapshot snapshot = MakeSnapshot();
		snapshot.partyInCombat = false;
		snapshot.selfInCombat = false;

		const BotCompanionResult result = machine.Evaluate(snapshot);
		REQUIRE(result.mode == BotCompanionMode::LeaderFollow);
		CHECK(result.modeReason == "party_out_of_combat");
		CHECK(result.hasAnchor);
		CHECK(result.anchor.kind == BotFollowAnchorKind::Leader);
		CHECK(result.anchor.guid == snapshot.leaderGuid);
		CHECK(result.anchorReason == "leader_anchor");
	}

	TEST_CASE("companion state machine treats human and bot leaders the same for travel follow", "[bot-companion][state]")
	{
		BotCompanionStateMachine machine;
		BotCompanionSnapshot snapshot = MakeSnapshot();
		snapshot.partyMembers[1].isBot = false;
		snapshot.partyInCombat = false;

		const BotCompanionResult result = machine.Evaluate(snapshot);
		REQUIRE(result.mode == BotCompanionMode::LeaderFollow);
		CHECK(result.anchor.guid == snapshot.leaderGuid);
		CHECK(result.anchor.label == "leader");
	}

	TEST_CASE("companion state machine switches to a role anchor when the party enters combat", "[bot-companion][state]")
	{
		BotCompanionStateMachine machine;
		BotCompanionSnapshot snapshot = MakeSnapshot();
		snapshot.partyInCombat = true;
		snapshot.partyMembers[1].isInCombat = true;

		const BotCompanionResult result = machine.Evaluate(snapshot);
		REQUIRE(result.mode == BotCompanionMode::CombatAnchor);
		CHECK(result.modeReason == "party_in_combat");
		CHECK(result.hasAnchor);
		CHECK(result.anchor.kind == BotFollowAnchorKind::Role);
		CHECK(result.anchor.guid == snapshot.leaderGuid);
		CHECK(result.anchorReason == "role_anchor_tank");
	}

	TEST_CASE("companion state machine regroups to the leader when the requested combat anchor loses awareness", "[bot-companion][state]")
	{
		BotCompanionStateMachine machine;
		BotCompanionSnapshot snapshot = MakeSnapshot();
		snapshot.partyInCombat = true;
		snapshot.partyMembers[1].isLeader = false;
		snapshot.partyMembers[1].role = BotCompanionRole::Healer;
		snapshot.partyMembers[1].isInCombat = true;

		BotCompanionMemberSnapshot tank;
		tank.guid = 40;
		tank.role = BotCompanionRole::Tank;
		tank.position = Vector3(12.0f, 0.0f, 0.0f);
		tank.hasPosition = true;
		tank.isAlive = true;
		tank.isAware = false;
		tank.isInCombat = true;
		tank.label = "tank";
		snapshot.partyMembers.push_back(tank);

		const BotCompanionResult result = machine.Evaluate(snapshot);
		REQUIRE(result.mode == BotCompanionMode::Regroup);
		CHECK(result.modeReason == "role_anchor_out_of_awareness");
		CHECK(result.hasAnchor);
		CHECK(result.anchor.kind == BotFollowAnchorKind::Leader);
		CHECK(result.anchor.guid == snapshot.leaderGuid);
		CHECK(result.anchorReason == "leader_regroup_anchor");
	}

	TEST_CASE("companion state machine reports stale combat anchors explicitly before regrouping", "[bot-companion][state]")
	{
		BotCompanionStateMachine machine;
		BotCompanionSnapshot snapshot = MakeSnapshot();
		snapshot.partyInCombat = true;
		snapshot.partyMembers[1].isInCombat = true;
		snapshot.partyMembers[1].role = BotCompanionRole::Healer;
		snapshot.previousState.mode = BotCompanionMode::CombatAnchor;
		snapshot.previousState.anchorGuid = 99;
		snapshot.previousState.anchorKind = BotFollowAnchorKind::Role;

		const BotCompanionResult result = machine.Evaluate(snapshot);
		REQUIRE(result.mode == BotCompanionMode::Regroup);
		CHECK(result.modeReason == "stale_combat_anchor");
		CHECK(result.anchor.guid == snapshot.leaderGuid);
	}

	TEST_CASE("companion state machine holds when the leader is missing from awareness during travel", "[bot-companion][state]")
	{
		BotCompanionStateMachine machine;
		BotCompanionSnapshot snapshot = MakeSnapshot();
		snapshot.partyMembers[1].isAware = false;
		snapshot.partyInCombat = false;

		const BotCompanionResult result = machine.Evaluate(snapshot);
		REQUIRE(result.mode == BotCompanionMode::Hold);
		CHECK(result.modeReason == "leader_out_of_awareness");
		CHECK_FALSE(result.hasAnchor);
		CHECK(result.anchorReason == "hold_position");
	}

	TEST_CASE("companion state machine holds for malformed self snapshots", "[bot-companion][state]")
	{
		BotCompanionStateMachine machine;
		BotCompanionSnapshot snapshot = MakeSnapshot();
		snapshot.partyMembers[0].hasPosition = false;

		const BotCompanionResult result = machine.Evaluate(snapshot);
		REQUIRE(result.mode == BotCompanionMode::Hold);
		CHECK(result.modeReason == "self_anchor_invalid");
		CHECK_FALSE(result.hasAnchor);
	}

	TEST_CASE("companion state machine holds when combat flags contradict each other", "[bot-companion][state]")
	{
		BotCompanionStateMachine machine;
		BotCompanionSnapshot snapshot = MakeSnapshot();
		snapshot.partyInCombat = true;
		snapshot.selfInCombat = false;
		snapshot.partyMembers[0].isInCombat = false;
		snapshot.partyMembers[1].isInCombat = false;
		snapshot.partyMembers[2].isInCombat = false;

		const BotCompanionResult result = machine.Evaluate(snapshot);
		REQUIRE(result.mode == BotCompanionMode::Hold);
		CHECK(result.modeReason == "contradictory_combat_state");
		CHECK(result.anchorReason == "hold_position");
	}

	TEST_CASE("companion state machine holds when the bot is effectively in a solo party", "[bot-companion][state]")
	{
		BotCompanionStateMachine machine;
		BotCompanionSnapshot snapshot;
		snapshot.selfGuid = 10;
		snapshot.leaderGuid = 10;

		BotCompanionMemberSnapshot self;
		self.guid = 10;
		self.role = BotCompanionRole::Healer;
		self.position = Vector3(0.0f, 0.0f, 0.0f);
		self.hasPosition = true;
		self.isSelf = true;
		self.isLeader = true;
		self.isAlive = true;
		self.isAware = true;
		snapshot.partyMembers.push_back(self);

		const BotCompanionResult result = machine.Evaluate(snapshot);
		REQUIRE(result.mode == BotCompanionMode::Hold);
		CHECK(result.modeReason == "solo_party");
	}

	TEST_CASE("companion state machine returns to leader follow as soon as combat clears and awareness recovers", "[bot-companion][state]")
	{
		BotCompanionStateMachine machine;
		BotCompanionSnapshot combatSnapshot = MakeSnapshot();
		combatSnapshot.partyInCombat = true;
		combatSnapshot.partyMembers[1].role = BotCompanionRole::Healer;
		combatSnapshot.partyMembers[1].isInCombat = true;
		combatSnapshot.previousState.mode = BotCompanionMode::CombatAnchor;
		combatSnapshot.previousState.anchorGuid = 77;
		combatSnapshot.previousState.anchorKind = BotFollowAnchorKind::Role;

		const BotCompanionResult regroup = machine.Evaluate(combatSnapshot);
		REQUIRE(regroup.mode == BotCompanionMode::Regroup);
		CHECK(regroup.modeReason == "stale_combat_anchor");

		BotCompanionSnapshot recoveredSnapshot = combatSnapshot;
		recoveredSnapshot.partyInCombat = false;
		recoveredSnapshot.selfInCombat = false;
		recoveredSnapshot.partyMembers[1].isAware = true;
		recoveredSnapshot.partyMembers[1].isLeader = true;
		recoveredSnapshot.partyMembers[1].role = BotCompanionRole::Tank;
		recoveredSnapshot.partyMembers[1].isInCombat = false;
		recoveredSnapshot.previousState.mode = regroup.mode;
		recoveredSnapshot.previousState.anchorGuid = regroup.anchor.guid;
		recoveredSnapshot.previousState.anchorKind = regroup.anchor.kind;

		const BotCompanionResult recovered = machine.Evaluate(recoveredSnapshot);
		REQUIRE(recovered.mode == BotCompanionMode::LeaderFollow);
		CHECK(recovered.modeReason == "party_out_of_combat");
		CHECK(recovered.anchor.guid == recoveredSnapshot.leaderGuid);
	}
}
