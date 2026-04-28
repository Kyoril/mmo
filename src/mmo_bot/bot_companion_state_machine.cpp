// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "bot_companion_state_machine.h"

#include "bot_movement_math.h"

namespace
{
	const mmo::BotCompanionMemberSnapshot* FindMember(const mmo::BotCompanionSnapshot& snapshot, const uint64 guid)
	{
		if (guid == 0)
		{
			return nullptr;
		}

		for (const mmo::BotCompanionMemberSnapshot& member : snapshot.partyMembers)
		{
			if (member.guid == guid)
			{
				return &member;
			}
		}

		return nullptr;
	}

	const mmo::BotCompanionMemberSnapshot* FindSelf(const mmo::BotCompanionSnapshot& snapshot)
	{
		if (const mmo::BotCompanionMemberSnapshot* self = FindMember(snapshot, snapshot.selfGuid))
		{
			return self;
		}

		for (const mmo::BotCompanionMemberSnapshot& member : snapshot.partyMembers)
		{
			if (member.isSelf)
			{
				return &member;
			}
		}

		return nullptr;
	}

	const mmo::BotCompanionMemberSnapshot* FindLeader(const mmo::BotCompanionSnapshot& snapshot)
	{
		if (const mmo::BotCompanionMemberSnapshot* leader = FindMember(snapshot, snapshot.leaderGuid))
		{
			return leader;
		}

		for (const mmo::BotCompanionMemberSnapshot& member : snapshot.partyMembers)
		{
			if (member.isLeader)
			{
				return &member;
			}
		}

		return nullptr;
	}

	bool HasUsableAnchorPosition(const mmo::BotCompanionMemberSnapshot& member)
	{
		return member.hasPosition && mmo::IsFiniteVector(member.position);
	}

	bool HasViableLeaderAnchor(const mmo::BotCompanionMemberSnapshot* leader, const uint64 selfGuid)
	{
		return leader
			&& leader->guid != 0
			&& leader->guid != selfGuid
			&& leader->isAware
			&& leader->isAlive
			&& HasUsableAnchorPosition(*leader);
	}

	bool HasAnyMemberInCombat(const mmo::BotCompanionSnapshot& snapshot)
	{
		for (const mmo::BotCompanionMemberSnapshot& member : snapshot.partyMembers)
		{
			if (member.isInCombat)
			{
				return true;
			}
		}

		return false;
	}

	bool HasContradictoryCombatState(const mmo::BotCompanionSnapshot& snapshot, const mmo::BotCompanionMemberSnapshot* self)
	{
		if (self && self->isInCombat != snapshot.selfInCombat)
		{
			return true;
		}

		return snapshot.partyInCombat && !snapshot.selfInCombat && !HasAnyMemberInCombat(snapshot);
	}

	mmo::BotFollowAnchor BuildAnchor(const mmo::BotCompanionMemberSnapshot& member, const mmo::BotFollowAnchorKind kind, const std::string& fallbackLabel = {})
	{
		mmo::BotFollowAnchor anchor;
		anchor.kind = kind;
		anchor.guid = member.guid;
		anchor.position = member.position;
		anchor.hasPosition = member.hasPosition;
		anchor.facing = member.facing;
		anchor.hasFacing = member.hasFacing;
		anchor.label = !member.label.empty() ? member.label : fallbackLabel;
		return anchor;
	}

	const char* DetermineRoleAnchorFailure(const mmo::BotCompanionSnapshot& snapshot)
	{
		const bool anyRoleAccepted = snapshot.desiredCombatRole == mmo::BotCompanionRole::None;
		bool sawRoleCandidate = false;
		bool sawAwarenessLoss = false;
		bool sawInvalidPosition = false;
		bool sawCombatReadinessLoss = false;

		for (const mmo::BotCompanionMemberSnapshot& member : snapshot.partyMembers)
		{
			if (member.isSelf)
			{
				continue;
			}

			if (!anyRoleAccepted && member.role != snapshot.desiredCombatRole)
			{
				continue;
			}

			sawRoleCandidate = true;
			if (!member.isAware)
			{
				sawAwarenessLoss = true;
				continue;
			}

			if (!HasUsableAnchorPosition(member))
			{
				sawInvalidPosition = true;
				continue;
			}

			if (!member.isAlive || !member.isInCombat)
			{
				sawCombatReadinessLoss = true;
			}
		}

		if (sawAwarenessLoss)
		{
			return "role_anchor_out_of_awareness";
		}

		if (sawInvalidPosition)
		{
			return "role_anchor_invalid_position";
		}

		if (sawCombatReadinessLoss)
		{
			return "role_anchor_not_combat_ready";
		}

		if (sawRoleCandidate)
		{
			return "role_anchor_missing";
		}

		return anyRoleAccepted ? "combat_anchor_missing" : "role_anchor_missing";
	}

	const mmo::BotCompanionMemberSnapshot* FindCombatAnchorCandidate(const mmo::BotCompanionSnapshot& snapshot)
	{
		const bool anyRoleAccepted = snapshot.desiredCombatRole == mmo::BotCompanionRole::None;
		const mmo::BotCompanionMemberSnapshot* fallback = nullptr;

		for (const mmo::BotCompanionMemberSnapshot& member : snapshot.partyMembers)
		{
			if (member.isSelf)
			{
				continue;
			}

			if (!anyRoleAccepted && member.role != snapshot.desiredCombatRole)
			{
				continue;
			}

			if (!member.isAware || !member.isAlive || !member.isInCombat || !HasUsableAnchorPosition(member))
			{
				continue;
			}

			if (member.isLeader)
			{
				return &member;
			}

			if (!fallback)
			{
				fallback = &member;
			}
		}

		return fallback;
	}

	bool PreviousCombatAnchorIsStale(const mmo::BotCompanionSnapshot& snapshot)
	{
		if (snapshot.previousState.mode != mmo::BotCompanionMode::CombatAnchor || snapshot.previousState.anchorGuid == 0)
		{
			return false;
		}

		const mmo::BotCompanionMemberSnapshot* member = FindMember(snapshot, snapshot.previousState.anchorGuid);
		return !member || !member->isAware || !member->isAlive || !HasUsableAnchorPosition(*member) || !member->isInCombat;
	}
}

namespace mmo
{
	const char* ToString(const BotCompanionMode mode) noexcept
	{
		switch (mode)
		{
		case BotCompanionMode::LeaderFollow:
			return "leader_follow";
		case BotCompanionMode::CombatAnchor:
			return "combat_anchor";
		case BotCompanionMode::Regroup:
			return "regroup";
		case BotCompanionMode::Hold:
		default:
			return "hold";
		}
	}

	const char* ToString(const BotCompanionRole role) noexcept
	{
		switch (role)
		{
		case BotCompanionRole::Tank:
			return "tank";
		case BotCompanionRole::Healer:
			return "healer";
		case BotCompanionRole::MeleeDps:
			return "melee_dps";
		case BotCompanionRole::RangedDps:
			return "ranged_dps";
		case BotCompanionRole::None:
		default:
			return "none";
		}
	}

	BotCompanionResult BotCompanionStateMachine::Evaluate(const BotCompanionSnapshot& snapshot) const
	{
		BotCompanionResult result;
		result.mode = BotCompanionMode::Hold;
		result.modeReason = "hold_position";
		result.anchorReason = "hold_position";

		const BotCompanionMemberSnapshot* self = FindSelf(snapshot);
		if (snapshot.selfGuid == 0 || !self || !self->isAware || !HasUsableAnchorPosition(*self))
		{
			result.modeReason = !self ? "missing_self" : (!self->isAware ? "self_out_of_awareness" : "self_anchor_invalid");
			return result;
		}

		if (snapshot.leaderGuid == 0)
		{
			result.modeReason = "leader_guid_missing";
			return result;
		}

		if (snapshot.leaderGuid == snapshot.selfGuid)
		{
			result.modeReason = snapshot.partyMembers.size() <= 1 ? "solo_party" : "self_is_leader";
			return result;
		}

		const BotCompanionMemberSnapshot* leader = FindLeader(snapshot);
		if (!leader)
		{
			result.modeReason = "leader_not_in_party";
			return result;
		}

		if (HasContradictoryCombatState(snapshot, self))
		{
			result.modeReason = "contradictory_combat_state";
			return result;
		}

		const bool inCombat = snapshot.partyInCombat || snapshot.selfInCombat || HasAnyMemberInCombat(snapshot);
		if (!inCombat)
		{
			if (!HasViableLeaderAnchor(leader, snapshot.selfGuid))
			{
				result.modeReason = !leader->isAware ? "leader_out_of_awareness" : "leader_anchor_invalid";
				return result;
			}

			result.mode = BotCompanionMode::LeaderFollow;
			result.modeReason = "party_out_of_combat";
			result.anchor = BuildAnchor(*leader, BotFollowAnchorKind::Leader, "leader");
			result.hasAnchor = true;
			result.anchorReason = "leader_anchor";
			return result;
		}

		if (const BotCompanionMemberSnapshot* combatAnchor = FindCombatAnchorCandidate(snapshot))
		{
			result.mode = BotCompanionMode::CombatAnchor;
			result.modeReason = "party_in_combat";
			result.anchor = BuildAnchor(*combatAnchor, BotFollowAnchorKind::Role, ToString(combatAnchor->role));
			result.hasAnchor = true;
			result.anchorReason = std::string("role_anchor_") + ToString(combatAnchor->role);
			return result;
		}

		const char* failureReason = PreviousCombatAnchorIsStale(snapshot)
			? "stale_combat_anchor"
			: DetermineRoleAnchorFailure(snapshot);

		if (HasViableLeaderAnchor(leader, snapshot.selfGuid))
		{
			result.mode = BotCompanionMode::Regroup;
			result.modeReason = failureReason;
			result.anchor = BuildAnchor(*leader, BotFollowAnchorKind::Leader, "leader");
			result.hasAnchor = true;
			result.anchorReason = "leader_regroup_anchor";
			return result;
		}

		result.mode = BotCompanionMode::Hold;
		result.modeReason = failureReason;
		result.anchorReason = "hold_position";
		return result;
	}
}
