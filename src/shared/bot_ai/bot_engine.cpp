// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_engine.h"

#include "bot_ai_context.h"
#include "bot_ai_registry.h"

#include <algorithm>
#include <utility>

namespace mmo
{
	BotEngine::BotEngine(const BotAiRegistry& registry, std::string name, BotEngineSettings settings)
		: m_registry(registry)
		, m_name(std::move(name))
		, m_settings(settings)
	{
	}

	void BotEngine::AddStrategy(const std::string& name)
	{
		const BotStrategy* strategy = m_registry.FindStrategy(name);
		if (!strategy)
		{
			return;
		}

		m_strategyNames.push_back(name);

		// Trigger nodes are flattened on the way in rather than walked per tick. Strategies are
		// added once and ticked forever, so resolving names now turns a map lookup per trigger
		// per tick into a pointer dereference.
		for (const BotTriggerNode& node : strategy->GetTriggerNodes())
		{
			const BotTrigger* trigger = m_registry.FindTrigger(node.trigger);
			if (!trigger)
			{
				continue;
			}

			TriggerSlot slot;
			slot.trigger = trigger;
			slot.actions = node.actions;
			m_triggerSlots.push_back(std::move(slot));
		}

		for (const BotNextAction& next : strategy->GetDefaultActions())
		{
			m_defaultActions.push_back(next);
		}

		for (const std::string& multiplierName : strategy->GetMultipliers())
		{
			if (const BotMultiplier* multiplier = m_registry.FindMultiplier(multiplierName))
			{
				m_multipliers.push_back(multiplier);
			}
		}
	}

	void BotEngine::Reset()
	{
		m_queue.Clear();

		for (TriggerSlot& slot : m_triggerSlots)
		{
			slot.everChecked = false;
			slot.lastCheckMs = 0;
		}
	}

	void BotEngine::ProcessTriggers(BotAiContext& context)
	{
		const GameTime now = context.GetNow();

		for (TriggerSlot& slot : m_triggerSlots)
		{
			const GameTime interval = slot.trigger->GetCheckIntervalMs();
			if (interval > 0 && slot.everChecked && now < slot.lastCheckMs + interval)
			{
				continue;
			}

			slot.lastCheckMs = now;
			slot.everChecked = true;

			if (!slot.trigger->IsActive(context))
			{
				continue;
			}

			for (const BotNextAction& next : slot.actions)
			{
				m_queue.Push(next, now);
			}
		}
	}

	void BotEngine::PushDefaultActions(BotAiContext& context)
	{
		const GameTime now = context.GetNow();
		for (const BotNextAction& next : m_defaultActions)
		{
			m_queue.Push(next, now);
		}
	}

	float BotEngine::ScoreAction(BotAiContext& context, const std::string& actionName, float relevance) const
	{
		for (const BotMultiplier* multiplier : m_multipliers)
		{
			const float factor = multiplier->GetValue(context, actionName);
			if (factor <= 0.0f)
			{
				// A veto is reported as a negative score rather than as a zero relevance. Zero is a
				// legitimate relevance - it is the whole Idle band - so conflating the two would
				// silently drop every fallback action a strategy has.
				return -1.0f;
			}

			relevance *= factor;
		}

		return relevance;
	}

	bool BotEngine::DoNextAction(BotAiContext& context)
	{
		const GameTime now = context.GetNow();

		ProcessTriggers(context);
		PushDefaultActions(context);

		const BotActionQueue::ScoreFn score = [this, &context](const std::string& actionName, const float relevance)
			{
				return ScoreAction(context, actionName, relevance);
			};

		bool executed = false;

		for (uint32 iteration = 0; iteration < m_settings.maxIterations; ++iteration)
		{
			BotActionBasket basket;
			if (!m_queue.PopBest(score, basket))
			{
				break;
			}

			BotAction* action = m_registry.FindAction(basket.action);
			if (!action)
			{
				continue;
			}

			if (!action->IsUseful(context))
			{
				continue;
			}

			if (!action->IsPossible(context))
			{
				const BotNextActionList prerequisites = action->GetPrerequisites();
				if (prerequisites.empty())
				{
					// Impossible with no way to make it possible. Dropping it is the only option;
					// putting it back would spin until the iteration cap and then drop it anyway.
					continue;
				}

				for (const BotNextAction& next : prerequisites)
				{
					m_queue.Push(next.action, basket.relevance + m_settings.prerequisiteBoost, now);
				}

				// Wait for the prerequisite rather than retry this tick: whether it worked shows
				// up in the world, and the world is only re-read at the start of the next tick.
				m_queue.Push(basket.action, basket.relevance + m_settings.retryBoost, now);
				continue;
			}

			if (!action->Execute(context))
			{
				for (const BotNextAction& next : action->GetAlternatives())
				{
					m_queue.Push(next.action, basket.relevance + m_settings.alternativeBoost, now);
				}
				continue;
			}

			context.SetLastAction(basket.action);
			ActionExecuted(basket.action, basket.effectiveRelevance);

			for (const BotNextAction& next : action->GetContinuers())
			{
				m_queue.Push(next, now);
			}

			executed = true;
			break;
		}

		m_queue.RemoveExpired(now, m_settings.actionExpiryMs);
		return executed;
	}
}
