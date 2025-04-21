// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "proto_data/project.h"

namespace mmo
{
	class GamePlayerS;

	/// Manages game conditions and offers methods to check whether a given condition is fulfilled by a player.
	class ConditionMgr final : public NonCopyable
	{
	public:
		explicit ConditionMgr(const proto::ConditionManager& data);

	public:
		/// Determines whether a given player fulfills a given condition.
		/// @param player The player to check.
		/// @param conditionId The ID of the condition to check.
		/// @returns true if condition is fulfilled, false if not or condition does not exist.
		bool PlayerMeetsCondition(GamePlayerS& player, uint32 conditionId);

	private:
		const proto::ConditionManager& m_data;
	};
}
