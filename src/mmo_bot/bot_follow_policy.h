// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#pragma once

#include "bot_follow_types.h"

namespace mmo
{
	class BotFollowPolicy final
	{
	public:
		explicit BotFollowPolicy(BotFollowConfig config = {});

		[[nodiscard]] const BotFollowConfig& GetConfig() const noexcept { return m_config; }
		[[nodiscard]] BotFollowDecision Evaluate(const BotFollowPolicyInput& input) const;

	private:
		[[nodiscard]] bool HasUsablePath(const BotFollowPath& path) const;
		[[nodiscard]] std::size_t FindSteeringWaypointIndex(const BotFollowSample& self, const BotFollowPath& path) const;
		[[nodiscard]] Vector3 ResolveSteeringTarget(const BotFollowPath& path, std::size_t waypointIndex) const;
		[[nodiscard]] const char* DetermineRepathReason(const BotFollowPolicyInput& input) const;

	private:
		BotFollowConfig m_config;
	};
}
