// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_next_action.h"

#include "base/non_copyable.h"

#include <string>
#include <utility>
#include <vector>

namespace mmo
{
	/// A trigger and what it asks for when it fires.
	struct BotTriggerNode final
	{
		std::string trigger;
		BotNextActionList actions;
	};

	/// A named bundle of "when this, do that" rules, plus any relevance filters that apply while
	/// the bundle is active.
	///
	/// Strategies compose by layering: an engine holds several at once and simply merges their
	/// trigger nodes, so a class rotation, a survival strategy and a travel strategy can be
	/// stacked without knowing about each other. Ordering between them is expressed purely
	/// through relevance.
	class BotStrategy : public NonCopyable
	{
	public:
		explicit BotStrategy(std::string name)
			: m_name(std::move(name))
		{
		}

		virtual ~BotStrategy() = default;

		[[nodiscard]] const std::string& GetName() const { return m_name; }

		/// Trigger-to-action rules this strategy contributes.
		[[nodiscard]] virtual std::vector<BotTriggerNode> GetTriggerNodes() const = 0;

		/// Actions pushed every tick regardless of any trigger. Use sparingly and at a low
		/// relevance - this is for fallbacks, not for ordinary behaviour.
		[[nodiscard]] virtual BotNextActionList GetDefaultActions() const { return {}; }

		/// Names of multipliers that apply while this strategy is active.
		[[nodiscard]] virtual std::vector<std::string> GetMultipliers() const { return {}; }

	private:
		std::string m_name;
	};
}
