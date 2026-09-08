// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace mmo
{
	/// An action to run, named rather than pointed at, together with how much it matters.
	///
	/// Naming rather than pointing is what lets strategies be defined in data: a trigger can ask
	/// for "approach_target" without the definition of that action existing yet, and the same
	/// name can resolve to a different implementation for a different bot.
	struct BotNextAction final
	{
		std::string action;
		float relevance { 0.0f };

		BotNextAction() = default;

		BotNextAction(std::string actionName, const float actionRelevance)
			: action(std::move(actionName))
			, relevance(actionRelevance)
		{
		}
	};

	using BotNextActionList = std::vector<BotNextAction>;
}
