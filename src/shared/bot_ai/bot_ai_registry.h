// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_action.h"
#include "bot_multiplier.h"
#include "bot_strategy.h"
#include "bot_trigger.h"

#include "base/non_copyable.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mmo
{
	/// Name-to-definition lookup for everything an engine is built out of.
	///
	/// Definitions are shared and stateless, so the registry owns one instance of each and hands
	/// out raw pointers; per-bot state lives on the context. A swarm of a thousand bots therefore
	/// holds exactly one approach-target action.
	///
	/// Registration by name rather than by type is what lets behaviour be authored outside C++:
	/// a strategy asks for a name and gets whatever was registered under it, whether that was
	/// built here or loaded from a script.
	class BotAiRegistry final : public NonCopyable
	{
	public:
		void RegisterAction(std::unique_ptr<BotAction> action);
		void RegisterTrigger(std::unique_ptr<BotTrigger> trigger);
		void RegisterMultiplier(std::unique_ptr<BotMultiplier> multiplier);
		void RegisterStrategy(std::unique_ptr<BotStrategy> strategy);

		[[nodiscard]] BotAction* FindAction(const std::string& name) const;
		[[nodiscard]] BotTrigger* FindTrigger(const std::string& name) const;
		[[nodiscard]] BotMultiplier* FindMultiplier(const std::string& name) const;
		[[nodiscard]] BotStrategy* FindStrategy(const std::string& name) const;

		/// Names of everything a strategy or an action chain refers to that is not registered.
		/// Called once after the registry is populated: a typo would otherwise show up as a bot
		/// that quietly never does one particular thing.
		[[nodiscard]] std::vector<std::string> FindDanglingReferences() const;

	private:
		std::unordered_map<std::string, std::unique_ptr<BotAction>> m_actions;
		std::unordered_map<std::string, std::unique_ptr<BotTrigger>> m_triggers;
		std::unordered_map<std::string, std::unique_ptr<BotMultiplier>> m_multipliers;
		std::unordered_map<std::string, std::unique_ptr<BotStrategy>> m_strategies;
	};
}
