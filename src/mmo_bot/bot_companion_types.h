// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#pragma once

#include "bot_follow_types.h"

#include <string>
#include <vector>

namespace mmo
{
	enum class BotCompanionMode : uint8
	{
		Hold = 0,
		LeaderFollow = 1,
		CombatAnchor = 2,
		Regroup = 3,
	};

	enum class BotCompanionRole : uint8
	{
		None = 0,
		Tank = 1,
		Healer = 2,
		MeleeDps = 3,
		RangedDps = 4,
	};

	struct BotCompanionMemberSnapshot final
	{
		uint64 guid { 0 };
		BotCompanionRole role { BotCompanionRole::None };
		Vector3 position { Vector3::Zero };
		bool hasPosition { false };
		Radian facing { 0.0f };
		bool hasFacing { false };
		bool isSelf { false };
		bool isLeader { false };
		bool isBot { false };
		bool isAlive { true };
		bool isAware { false };
		bool isInCombat { false };
		uint64 targetGuid { 0 };
		std::string label;
	};

	struct BotCompanionState final
	{
		BotCompanionMode mode { BotCompanionMode::Hold };
		uint64 anchorGuid { 0 };
		BotFollowAnchorKind anchorKind { BotFollowAnchorKind::Custom };
	};

	struct BotCompanionSnapshot final
	{
		uint64 selfGuid { 0 };
		uint64 leaderGuid { 0 };
		BotCompanionRole desiredCombatRole { BotCompanionRole::None };
		bool selfInCombat { false };
		bool partyInCombat { false };
		std::vector<BotCompanionMemberSnapshot> partyMembers;
		BotCompanionState previousState;
	};

	struct BotCompanionResult final
	{
		BotCompanionMode mode { BotCompanionMode::Hold };
		BotFollowAnchor anchor;
		bool hasAnchor { false };
		std::string modeReason;
		std::string anchorReason;
	};

	[[nodiscard]] const char* ToString(BotCompanionMode mode) noexcept;
	[[nodiscard]] const char* ToString(BotCompanionRole role) noexcept;
}
