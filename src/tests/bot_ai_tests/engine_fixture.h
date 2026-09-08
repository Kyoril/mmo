// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_ai/bot_action.h"
#include "bot_ai/bot_ai_context.h"
#include "bot_ai/bot_ai_registry.h"
#include "bot_ai/bot_engine.h"
#include "bot_ai/bot_multiplier.h"
#include "bot_ai/bot_relevance.h"
#include "bot_ai/bot_strategy.h"
#include "bot_ai/bot_trigger.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace mmo::bot_ai_tests
{
	/// Records every action that ran, so a test can assert on the decision the engine made
	/// rather than on a side effect of it.
	struct ExecutionLog final
	{
		std::vector<std::string> executed;

		[[nodiscard]] bool Ran(const std::string& action) const
		{
			return std::find(executed.begin(), executed.end(), action) != executed.end();
		}
	};

	/// An action whose usefulness, possibility, outcome and chains are all set by the test.
	class ScriptedAction final : public BotAction
	{
	public:
		ScriptedAction(std::string name, ExecutionLog& log)
			: BotAction(std::move(name))
			, m_log(log)
		{
		}

		bool useful { true };
		bool possible { true };
		bool succeeds { true };
		BotNextActionList prerequisites;
		BotNextActionList alternatives;
		BotNextActionList continuers;

		[[nodiscard]] bool IsUseful(BotAiContext&) const override { return useful; }
		[[nodiscard]] bool IsPossible(BotAiContext&) const override { return possible; }

		bool Execute(BotAiContext&) override
		{
			m_log.executed.push_back(GetName());
			return succeeds;
		}

		[[nodiscard]] BotNextActionList GetPrerequisites() const override { return prerequisites; }
		[[nodiscard]] BotNextActionList GetAlternatives() const override { return alternatives; }
		[[nodiscard]] BotNextActionList GetContinuers() const override { return continuers; }

	private:
		ExecutionLog& m_log;
	};

	/// A trigger the test switches on and off directly.
	class ScriptedTrigger final : public BotTrigger
	{
	public:
		ScriptedTrigger(std::string name, const bool startsActive, const GameTime checkIntervalMs = 0)
			: BotTrigger(std::move(name), checkIntervalMs)
			, active(startsActive)
		{
		}

		bool active { false };
		mutable uint32 checkCount { 0 };

		[[nodiscard]] bool IsActive(BotAiContext&) const override
		{
			++checkCount;
			return active;
		}
	};

	/// A multiplier driven by a lambda supplied by the test.
	class ScriptedMultiplier final : public BotMultiplier
	{
	public:
		using Fn = std::function<float(const std::string&)>;

		ScriptedMultiplier(std::string name, Fn fn)
			: BotMultiplier(std::move(name))
			, m_fn(std::move(fn))
		{
		}

		[[nodiscard]] float GetValue(BotAiContext&, const std::string& actionName) const override
		{
			return m_fn(actionName);
		}

	private:
		Fn m_fn;
	};

	/// A strategy built from literal trigger nodes rather than from code.
	class ScriptedStrategy final : public BotStrategy
	{
	public:
		explicit ScriptedStrategy(std::string name)
			: BotStrategy(std::move(name))
		{
		}

		std::vector<BotTriggerNode> nodes;
		BotNextActionList defaults;
		std::vector<std::string> multipliers;

		[[nodiscard]] std::vector<BotTriggerNode> GetTriggerNodes() const override { return nodes; }
		[[nodiscard]] BotNextActionList GetDefaultActions() const override { return defaults; }
		[[nodiscard]] std::vector<std::string> GetMultipliers() const override { return multipliers; }
	};

	/// Registry plus context plus engine, wired together, with raw pointers back to the scripted
	/// definitions so a test can flip their behaviour between ticks.
	class EngineFixture final
	{
	public:
		EngineFixture()
			: context(nullptr, 0)
		{
		}

		ScriptedAction& AddAction(const std::string& name)
		{
			auto action = std::make_unique<ScriptedAction>(name, log);
			ScriptedAction& ref = *action;
			registry.RegisterAction(std::move(action));
			return ref;
		}

		ScriptedTrigger& AddTrigger(const std::string& name, const bool active, const GameTime intervalMs = 0)
		{
			auto trigger = std::make_unique<ScriptedTrigger>(name, active, intervalMs);
			ScriptedTrigger& ref = *trigger;
			registry.RegisterTrigger(std::move(trigger));
			return ref;
		}

		ScriptedMultiplier& AddMultiplier(const std::string& name, ScriptedMultiplier::Fn fn)
		{
			auto multiplier = std::make_unique<ScriptedMultiplier>(name, std::move(fn));
			ScriptedMultiplier& ref = *multiplier;
			registry.RegisterMultiplier(std::move(multiplier));
			return ref;
		}

		ScriptedStrategy& AddStrategy(const std::string& name)
		{
			auto strategy = std::make_unique<ScriptedStrategy>(name);
			ScriptedStrategy& ref = *strategy;
			registry.RegisterStrategy(std::move(strategy));
			return ref;
		}

		/// Builds the engine. Call after every definition is registered, because AddStrategy
		/// resolves names once.
		BotEngine& Build(const std::string& strategyName, const BotEngineSettings settings = {})
		{
			engine = std::make_unique<BotEngine>(registry, "test", settings);
			engine->AddStrategy(strategyName);
			return *engine;
		}

		bool Tick(const GameTime nowMs)
		{
			context.SetNow(nowMs);
			return engine->DoNextAction(context);
		}

		BotAiRegistry registry;
		BotAiContext context;
		ExecutionLog log;
		std::unique_ptr<BotEngine> engine;
	};
}
