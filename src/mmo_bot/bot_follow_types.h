// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/radian.h"
#include "math/vector3.h"

#include <cstddef>
#include <string>
#include <vector>

namespace mmo
{
	enum class BotFollowAnchorKind : uint8
	{
		Leader = 0,
		Role = 1,
		Custom = 2,
	};

	struct BotFollowAnchor final
	{
		BotFollowAnchorKind kind { BotFollowAnchorKind::Custom };
		uint64 guid { 0 };
		Vector3 position { Vector3::Zero };
		bool hasPosition { false };
		Radian facing { 0.0f };
		bool hasFacing { false };
		std::string label;
	};

	struct BotFollowPath final
	{
		std::vector<Vector3> points;
	};

	struct BotFollowSample final
	{
		Vector3 position { Vector3::Zero };
		Radian facing { 0.0f };
		GameTime timestamp { 0 };
		bool isMoving { false };
		bool isValid { false };
	};

	enum class BotFollowDecisionType : uint8
	{
		Hold = 0,
		Advance = 1,
		Repath = 2,
		Abort = 3,
		Stuck = 4,
	};

	struct BotFollowState final
	{
		bool isMoving { false };
		bool hasLastRepathAnchorPosition { false };
		Vector3 lastRepathAnchorPosition { Vector3::Zero };
		GameTime lastRepathTime { 0 };
		bool hasLastProgressPosition { false };
		Vector3 lastProgressPosition { Vector3::Zero };
		GameTime lastProgressTime { 0 };
		bool hasLastDistanceToAnchor { false };
		float lastDistanceToAnchor { 0.0f };
	};

	struct BotFollowConfig final
	{
		float holdDistance { 2.5f };
		float leaveDistance { 3.5f };
		float repathAnchorDriftDistance { 1.75f };
		GameTime repathRefreshMs { 500 };
		float waypointAcceptanceRadius { 0.5f };
		float turnSmoothingThresholdRadians { 0.524f };
		float turnSmoothingDistance { 0.5f };
		float progressDistanceEpsilon { 0.35f };
		GameTime nonProgressTimeoutMs { 1500 };
	};

	struct BotFollowPolicyInput final
	{
		BotFollowAnchor anchor;
		BotFollowSample self;
		BotFollowPath path;
		BotFollowState state;
		GameTime now { 0 };
	};

	struct BotFollowDecision final
	{
		BotFollowDecisionType type { BotFollowDecisionType::Hold };
		std::string reason;
		float distanceToAnchor { 0.0f };
		bool shouldStop { false };
		bool shouldRepath { false };
		Vector3 steeringTarget { Vector3::Zero };
		bool hasSteeringTarget { false };
		Radian desiredFacing { 0.0f };
		bool hasDesiredFacing { false };
		std::size_t steeringWaypointIndex { 0 };
	};
}
