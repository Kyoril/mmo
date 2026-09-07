// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_ai_registry.h"

#include "base/macros.h"

namespace mmo
{
	namespace
	{
		template <typename T>
		void insertDefinition(std::unordered_map<std::string, std::unique_ptr<T>>& target, std::unique_ptr<T> value)
		{
			ASSERT(value);

			const std::string name = value->GetName();
			ASSERT(!name.empty());

			// A duplicate registration is always a mistake: the loser would silently never run.
			ASSERT(target.find(name) == target.end());
			target[name] = std::move(value);
		}

		template <typename T>
		T* findDefinition(const std::unordered_map<std::string, std::unique_ptr<T>>& source, const std::string& name)
		{
			const auto it = source.find(name);
			return it == source.end() ? nullptr : it->second.get();
		}
	}

	void BotAiRegistry::RegisterAction(std::unique_ptr<BotAction> action)
	{
		insertDefinition(m_actions, std::move(action));
	}

	void BotAiRegistry::RegisterTrigger(std::unique_ptr<BotTrigger> trigger)
	{
		insertDefinition(m_triggers, std::move(trigger));
	}

	void BotAiRegistry::RegisterMultiplier(std::unique_ptr<BotMultiplier> multiplier)
	{
		insertDefinition(m_multipliers, std::move(multiplier));
	}

	void BotAiRegistry::RegisterStrategy(std::unique_ptr<BotStrategy> strategy)
	{
		insertDefinition(m_strategies, std::move(strategy));
	}

	BotAction* BotAiRegistry::FindAction(const std::string& name) const
	{
		return findDefinition(m_actions, name);
	}

	BotTrigger* BotAiRegistry::FindTrigger(const std::string& name) const
	{
		return findDefinition(m_triggers, name);
	}

	BotMultiplier* BotAiRegistry::FindMultiplier(const std::string& name) const
	{
		return findDefinition(m_multipliers, name);
	}

	BotStrategy* BotAiRegistry::FindStrategy(const std::string& name) const
	{
		return findDefinition(m_strategies, name);
	}

	std::vector<std::string> BotAiRegistry::FindDanglingReferences() const
	{
		std::vector<std::string> dangling;

		const auto checkAction = [this, &dangling](const BotNextAction& next)
			{
				if (!FindAction(next.action))
				{
					dangling.push_back("action:" + next.action);
				}
			};

		for (const auto& strategyEntry : m_strategies)
		{
			const BotStrategy& strategy = *strategyEntry.second;

			for (const BotTriggerNode& node : strategy.GetTriggerNodes())
			{
				if (!FindTrigger(node.trigger))
				{
					dangling.push_back("trigger:" + node.trigger);
				}

				for (const BotNextAction& next : node.actions)
				{
					checkAction(next);
				}
			}

			for (const BotNextAction& next : strategy.GetDefaultActions())
			{
				checkAction(next);
			}

			for (const std::string& multiplier : strategy.GetMultipliers())
			{
				if (!FindMultiplier(multiplier))
				{
					dangling.push_back("multiplier:" + multiplier);
				}
			}
		}

		// Chains are checked from the action side, so that a link is validated even when nothing
		// currently triggers the first action in the chain.
		for (const auto& actionEntry : m_actions)
		{
			const BotAction& action = *actionEntry.second;

			for (const BotNextAction& next : action.GetPrerequisites())
			{
				checkAction(next);
			}

			for (const BotNextAction& next : action.GetAlternatives())
			{
				checkAction(next);
			}

			for (const BotNextAction& next : action.GetContinuers())
			{
				checkAction(next);
			}
		}

		return dangling;
	}
}
